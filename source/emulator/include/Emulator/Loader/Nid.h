#ifndef EMULATOR_INCLUDE_EMULATOR_LOADER_NID_H_
#define EMULATOR_INCLUDE_EMULATOR_LOADER_NID_H_

#include "Kyty/Core/Common.h"
#include "Kyty/Core/String.h"

#include "Emulator/Common.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Loader {

// Hash a symbol name to its 11-character NID, as found in sce dynamic
// symbol tables ("<nid>#<lib>#<module>")
String NidHash(const char* symbol);

} // namespace Kyty::Loader

#endif // KYTY_EMU_ENABLED

#endif /* EMULATOR_INCLUDE_EMULATOR_LOADER_NID_H_ */
