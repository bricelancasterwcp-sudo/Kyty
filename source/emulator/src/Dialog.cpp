#include "Emulator/Dialog.h"

#include "Kyty/Core/String.h"

#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/Libs.h"

#ifdef KYTY_EMU_ENABLED

// NOLINTNEXTLINE(modernize-concat-nested-namespaces)
namespace Kyty::Libs::Dialog {

namespace CommonDialog {

LIB_NAME("CommonDialog", "CommonDialog");

int KYTY_SYSV_ABI CommonDialogInitialize()
{
	PRINT_NAME();

	return OK;
}

} // namespace CommonDialog

namespace SaveDataDialog {

LIB_NAME("SaveDataDialog", "SaveDataDialog");

int KYTY_SYSV_ABI SaveDataDialogUpdateStatus()
{
	PRINT_NAME();

	return 0;
}

int KYTY_SYSV_ABI SaveDataDialogTerminate()
{
	PRINT_NAME();

	return 0;
}

int KYTY_SYSV_ABI SaveDataDialogProgressBarSetValue(int target, uint32_t rate)
{
	PRINT_NAME();

	printf("\t target = %d\n", target);
	printf("\t rate   = %u\n", rate);

	return OK;
}

} // namespace SaveDataDialog

namespace MsgDialog {

LIB_NAME("MsgDialog", "MsgDialog");

constexpr int STATUS_NONE        = 0;
constexpr int STATUS_INITIALIZED = 1;
constexpr int STATUS_FINISHED    = 3;

constexpr int BUTTON_ID_INVALID = 0;
constexpr int BUTTON_ID_OK      = 1; // also YES / BUTTON1
constexpr int BUTTON_ID_NO      = 2; // also CANCEL / BUTTON2

constexpr int MODE_USER_MSG     = 1;
constexpr int MODE_PROGRESS_BAR = 2;
constexpr int MODE_SYSTEM_MSG   = 3;

struct CommonDialogBaseParam
{
	size_t   size;
	uint8_t  reserved[36];
	uint32_t magic;
};

struct MsgDialogButtonsParam
{
	const char* msg1;
	const char* msg2;
	char        reserved[32];
};

struct MsgDialogUserMessageParam
{
	int32_t                button_type;
	int32_t                pad;
	const char*            msg;
	MsgDialogButtonsParam* buttons_param;
	char                   reserved[24];
};

struct MsgDialogProgressBarParam
{
	int32_t     bar_type;
	int32_t     pad;
	const char* msg;
	char        reserved[64];
};

struct MsgDialogSystemMessageParam
{
	int32_t sys_msg_type;
	char    reserved[32];
};

struct MsgDialogParam
{
	CommonDialogBaseParam        base_param;
	size_t                       size;
	int32_t                      mode;
	int32_t                      pad;
	MsgDialogUserMessageParam*   user_msg_param;
	MsgDialogProgressBarParam*   prog_bar_param;
	MsgDialogSystemMessageParam* sys_msg_param;
	int32_t                      user_id;
	char                         reserved[40];
	int32_t                      pad2;
};

struct MsgDialogResult
{
	int32_t mode;
	int32_t result;
	int32_t button_id;
	char    reserved[32];
};

// There is no interactive dialog ui yet: the message is logged and the
// focused/default button is auto-selected, so guest flows keep moving
static int g_status         = STATUS_NONE;
static int g_last_mode      = 0;
static int g_last_button_id = BUTTON_ID_INVALID;

// button type -> the button a real dialog would have focused by default
static int default_button_for(int button_type)
{
	switch (button_type)
	{
		case 0: return BUTTON_ID_OK;      // OK
		case 1: return BUTTON_ID_OK;      // YESNO (focus yes)
		case 3: return BUTTON_ID_OK;      // OK_CANCEL (focus ok)
		case 7: return BUTTON_ID_NO;      // YESNO_FOCUS_NO
		case 8: return BUTTON_ID_NO;      // OK_CANCEL_FOCUS_CANCEL
		case 9: return BUTTON_ID_OK;      // 2BUTTONS
		default: return BUTTON_ID_INVALID; // NONE / WAIT variants
	}
}

int KYTY_SYSV_ABI MsgDialogInitialize()
{
	PRINT_NAME();

	g_status = STATUS_INITIALIZED;

	return OK;
}

int KYTY_SYSV_ABI MsgDialogOpen(const MsgDialogParam* param)
{
	PRINT_NAME();

	if (param == nullptr)
	{
		return LibKernel::KERNEL_ERROR_EINVAL;
	}

	g_last_mode      = param->mode;
	g_last_button_id = BUTTON_ID_OK;

	switch (param->mode)
	{
		case MODE_USER_MSG:
			if (param->user_msg_param != nullptr)
			{
				printf("\t [MsgDialog] %s\n", (param->user_msg_param->msg != nullptr ? param->user_msg_param->msg : "(null)"));
				g_last_button_id = default_button_for(param->user_msg_param->button_type);
			}
			break;
		case MODE_PROGRESS_BAR:
			if (param->prog_bar_param != nullptr)
			{
				printf("\t [MsgDialog:progress] %s\n", (param->prog_bar_param->msg != nullptr ? param->prog_bar_param->msg : "(null)"));
			}
			break;
		case MODE_SYSTEM_MSG:
			if (param->sys_msg_param != nullptr)
			{
				printf("\t [MsgDialog:system] type = %d\n", param->sys_msg_param->sys_msg_type);
			}
			break;
		default: return LibKernel::KERNEL_ERROR_EINVAL;
	}

	g_status = STATUS_FINISHED;

	return OK;
}

int KYTY_SYSV_ABI MsgDialogUpdateStatus()
{
	// PRINT_NAME();

	return g_status;
}

int KYTY_SYSV_ABI MsgDialogClose()
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI MsgDialogGetResult(MsgDialogResult* result)
{
	PRINT_NAME();

	if (result == nullptr)
	{
		return LibKernel::KERNEL_ERROR_EINVAL;
	}

	result->mode      = g_last_mode;
	result->result    = 0;
	result->button_id = g_last_button_id;

	return OK;
}

int KYTY_SYSV_ABI MsgDialogTerminate()
{
	PRINT_NAME();

	g_status = STATUS_NONE;

	return OK;
}

} // namespace MsgDialog

} // namespace Kyty::Libs::Dialog

#endif // KYTY_EMU_ENABLED
