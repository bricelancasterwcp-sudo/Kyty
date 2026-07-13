#include "Emulator/Common.h"
#include "Emulator/Keyboard.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs {

LIB_VERSION("Keyboard", 1, "Keyboard", 1, 1);

LIB_DEFINE(InitKeyboard_1)
{
	PRINT_NAME_ENABLE(true);

	LIB_FUNC("wadT3QBCGY0", Keyboard::KeyboardInit);
	LIB_FUNC("HJ+KnEHcaxI", Keyboard::KeyboardOpen);
	LIB_FUNC("0LWei+c7RNc", Keyboard::KeyboardClose);
	LIB_FUNC("6HpE68bzX6M", Keyboard::KeyboardReadState);
	LIB_FUNC("yO9JwdRhtSA", Keyboard::KeyboardGetKey2Char);
}

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
