#include "Emulator/Common.h"
#include "Emulator/Dialog.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs {

namespace LibCommonDialog {

LIB_VERSION("CommonDialog", 1, "CommonDialog", 1, 1);

namespace CommonDialog = Dialog::CommonDialog;

LIB_DEFINE(InitDialog_1_CommonDialog)
{
	LIB_FUNC("uoUpLGNkygk", CommonDialog::CommonDialogInitialize);
}

} // namespace LibCommonDialog

namespace LibSaveDataDialog {

LIB_VERSION("SaveDataDialog", 1, "SaveDataDialog", 1, 1);

namespace SaveDataDialog = Dialog::SaveDataDialog;

LIB_DEFINE(InitDialog_1_SaveDataDialog)
{
	LIB_FUNC("KK3Bdg1RWK0", SaveDataDialog::SaveDataDialogUpdateStatus);
	LIB_FUNC("YuH2FA7azqQ", SaveDataDialog::SaveDataDialogTerminate);
	LIB_FUNC("hay1CfTmLyA", SaveDataDialog::SaveDataDialogProgressBarSetValue);
}

} // namespace LibSaveDataDialog

namespace LibMsgDialog {

LIB_VERSION("MsgDialog", 1, "MsgDialog", 1, 1);

namespace MsgDialog = Dialog::MsgDialog;

LIB_DEFINE(InitDialog_1_MsgDialog)
{
	LIB_FUNC("lDqxaY1UbEo", MsgDialog::MsgDialogInitialize);
	LIB_FUNC("b06Hh0DPEaE", MsgDialog::MsgDialogOpen);
	LIB_FUNC("6fIC3XKt2k0", MsgDialog::MsgDialogUpdateStatus);
	LIB_FUNC("HTrcDKlFKuM", MsgDialog::MsgDialogClose);
	LIB_FUNC("Lr8ovHH9l6A", MsgDialog::MsgDialogGetResult);
	LIB_FUNC("ePw-kqZmelo", MsgDialog::MsgDialogTerminate);
}

} // namespace LibMsgDialog

LIB_DEFINE(InitDialog_1)
{
	LibCommonDialog::InitDialog_1_CommonDialog(s);
	LibSaveDataDialog::InitDialog_1_SaveDataDialog(s);
	LibMsgDialog::InitDialog_1_MsgDialog(s);
}

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
