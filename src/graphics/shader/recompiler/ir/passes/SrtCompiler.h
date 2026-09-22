#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTCOMPILER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTCOMPILER_H_

#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::IR {

// A compiled, topologically-ordered form of a ResourcePlan's reachable SRT expression graph --
// see SrtCompiler.cpp's file comment for the full design rationale. Built once per unique shader
// (in ExtractResourcePlan), evaluated by SrtExecution on every draw instead of recursively
// re-walking the Inst graph through Evaluator::EvaluateInst's switch every time.
enum class CompiledOpKind : uint8_t {
	// Never produces a value. Used for: a Phi that survived ExtractResourcePlan's invariant
	// resolution (provably non-invariant, since ExtractResourcePlan already substitutes any
	// resolvable Phi -- see its Clone lambda), Undef*/Void, a CompositeConstructU32x2 or
	// IAddCarry32 node reached directly instead of through the one Extract case that knows how to
	// read it (EvaluateInst has no switch case for these on their own, matching today's
	// `default: break;`), and any opcode this compiler does not otherwise recognise.
	AlwaysFails,
	// A compile-time-resolved immediate value (an operand that was already a Value literal, not
	// an Inst -- resolved once here instead of re-checking IsImmediate()/GetType() every visit).
	Constant,
	GetUserData,
	GetShaderBase,
	// Every plain arithmetic/logic/comparison opcode that just needs its operands fetched and
	// dispatches on `opcode` -- same per-case bodies as EvaluateInst, just reached by indexing
	// this array instead of a recursive Arg()/EvaluateWide() chain. SelectU32/U1/F32 is NOT here
	// (see CompiledOpKind::Select below) -- this codebase's Evaluator resolves a Select's
	// predicate through the clean pass specifically and only evaluates the branch it selects,
	// unlike a plain eager ternary, so it needs its own execute-time handling.
	Generic,
	ExtractU64,
	// CompositeExtractU32x2 over a CompositeConstructU32x2 source: operand[0] is the already-
	// resolved slot for source->Arg(component) (EvaluateExtract never evaluates the Construct node
	// itself, just reaches through it), so this is a plain passthrough/copy.
	ExtractPassthrough,
	// CompositeExtractU32x2 over an IAddCarry32 source: operand[0]/[1] are the carry op's own two
	// operands; `component` (0=low, 1=high) selects which 32 bits of the 33-bit sum to return.
	ExtractCarryHalf,
	ReadFirstLane,
	ReadConst,
	RawRead,
	// SelectU32/SelectU1/SelectF32: operand[0] is the predicate, operand[1]/[2] the true/false
	// branches. See SrtExecution::Evaluate's case for why this needs its own kind instead of
	// folding into Generic -- the predicate is resolved against the clean pass rather than this
	// pass's own results, and only the branch it actually selects needs to be computed.
	Select,
};

inline constexpr uint32_t kInvalidSlot = UINT32_MAX;

struct CompiledOp {
	CompiledOpKind kind         = CompiledOpKind::AlwaysFails;
	ValueOpcode    opcode       = ValueOpcode::Void; // meaningful when kind == Generic
	uint8_t        num_operands = 0;
	// Sized for RawRead's ReadConstBuffer variant: handle low, high, offset, records, word3.
	std::array<uint32_t, 5> operands {kInvalidSlot, kInvalidSlot, kInvalidSlot, kInvalidSlot,
	                                  kInvalidSlot};
	uint64_t    immediate = 0;   // Constant: the value. GetUserData: sgpr offset.
	uint32_t    component = 0;   // CompositeExtract*: which component/half.
	bool        is_const_buffer_read = false; // RawRead: ReadConstBuffer vs LoadAddressU32.
	bool        srt_slot_clean = false; // ReadConst only: read from the clean pass's results
	                                    // instead of this pass's own.
};

struct CompiledSrtProgram {
	std::vector<CompiledOp> ops;
	// Compiled slot for each descriptor_sources[i].dwords[j], laid out the same shape as
	// ResourcePlan::descriptor_sources (parallel dword_count per source).
	std::vector<std::array<uint32_t, 8>> descriptor_source_slots;
	// Compiled slot for each srt_reads[i].value, parallel to ResourcePlan::srt_reads.
	std::vector<uint32_t> srt_read_slots;
	// Compiled slot for each control_flow[i].condition, parallel to ResourcePlan::control_flow.
	// kInvalidSlot when that block's condition is empty (unconditional edge).
	std::vector<uint32_t> control_flow_condition_slots;
};

// Builds a CompiledSrtProgram covering every Inst reachable from program.descriptor_sources,
// program.srt_reads, and program.control_flow -- called once, right after ExtractResourcePlan
// builds the final, stable Inst graph for a shader.
CompiledSrtProgram CompileSrtProgram(const ResourcePlan& program);

// Scratch buffers for SrtExecution, reused across calls the same way SrtWalker's Evaluator
// caches are (grows once to the largest program seen, never reallocates after).
struct SrtExecutorScratch {
	std::vector<uint64_t> results;
	std::vector<uint8_t>  computed;
};

class SrtExecution {
public:
	SrtExecution(const CompiledSrtProgram& compiled, const SrtRuntime& runtime,
	             SrtExecutorScratch& clean_scratch, SrtExecutorScratch& raw_scratch);

	bool Clean(uint32_t slot, uint64_t& value) { return Demand(true, slot, value); }
	bool Raw(uint32_t slot, uint64_t& value) { return Demand(false, slot, value); }

private:
	SrtExecution(const CompiledSrtProgram& compiled, SrtMemoryReader clean_read,
	             SrtMemoryReader raw_read, void* userdata, std::span<const uint32_t> user_data,
	             uint64_t shader_base, SrtExecutorScratch& clean, SrtExecutorScratch& raw,
	             uint32_t active_mask_slot);

	static constexpr uint8_t Untried  = 0;
	static constexpr uint8_t Computed = 1;
	static constexpr uint8_t Failed   = 2;
	static constexpr uint8_t Visiting = 3;

	void Reset(SrtExecutorScratch& scratch) const;
	bool Demand(bool clean, uint32_t slot, uint64_t& value);
	bool Operand(bool clean, const CompiledOp& op, uint32_t index, uint64_t& value);
	bool Evaluate(bool clean, uint32_t i, uint64_t& result);
	bool RawRead(bool clean, const CompiledOp& op, uint64_t& result);
	bool Generic(bool clean, const CompiledOp& op, uint64_t& result);

	const CompiledSrtProgram& m_compiled;
	SrtMemoryReader           m_clean_read;
	SrtMemoryReader           m_raw_read;
	void*                     m_userdata;
	std::span<const uint32_t> m_user_data;
	uint64_t                  m_shader_base;
	SrtExecutorScratch&       m_clean;
	SrtExecutorScratch&       m_raw;
	uint32_t                  m_active_mask_slot;
};

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif // EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTCOMPILER_H_
