#include "loader/symbolDatabase.h"

#include <cstdio>

namespace {

Loader::SymbolResolve MakeSymbol(const char* name, const char* module,
                                 Loader::SymbolType type) {
	return {name, "libSceTest", 1, module, 1, 0, type};
}

bool HasAddress(const Loader::SymbolRecord* record, uint64_t address) {
	return record != nullptr && record->vaddr == address;
}

} // namespace

int main() {
	Loader::SymbolDatabase symbols;
	const auto first  = MakeSymbol("shared", "libSceFirst", Loader::SymbolType::Func);
	const auto second = MakeSymbol("shared", "libSceSecond", Loader::SymbolType::Func);
	const auto object = MakeSymbol("shared", "libSceFirst", Loader::SymbolType::Object);

	symbols.Add(first, 0x1000);
	symbols.Add(second, 0x2000);
	symbols.Add(object, 0x3000);

	if (!HasAddress(symbols.FindByNid("shared", Loader::SymbolType::Func), 0x1000) ||
	    !HasAddress(symbols.FindByName("shared", Loader::SymbolType::Object), 0x3000) ||
	    !HasAddress(symbols.Find(second), 0x2000) ||
	    symbols.FindByNid("missing", Loader::SymbolType::Func) != nullptr) {
		std::fputs("symbol database lookup regression\n", stderr);
		return 1;
	}
	return 0;
}
