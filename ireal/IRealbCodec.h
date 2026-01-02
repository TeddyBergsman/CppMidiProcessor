#pragma once

#include <QString>

namespace ireal {

// iReal Pro (irealb://) token-string deobfuscation used in modern exports.
// Reference logic (reimplemented): remove "1r34LbKcu7", apply 50-char symmetric "hussle",
// then apply substitutions:
// - XyQ -> "   "
// - LZ  -> " |"
// - Kcl -> "| x"
//
// This converts the obfuscated token string into the canonical progression string.
QString deobfuscateIRealbTokens(const QString& rawTokenString);

} // namespace ireal

