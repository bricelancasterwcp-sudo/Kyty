local cfg = {
	ScreenWidth = 1920;
	ScreenHeight = 1080;
	Neo = true;
	VulkanValidationEnabled = false;
	ShaderValidationEnabled = false;
	ShaderOptimizationType = 'Performance';
	ShaderLogDirection = 'Silent';
	ShaderLogFolder = '_Shaders';
	CommandBufferDumpEnabled = false;
	CommandBufferDumpFolder = '_Buffers';
	PrintfDirection = 'Console';
	PrintfOutputFile = '_kyty.txt';
	ProfilerDirection = 'None';
	ProfilerOutputFile = '_profile.prof';
}

kyty_init(cfg);

kyty_mount('/home/brice/Projects/kyty-assets/openorbis/OpenOrbis/PS4Toolchain/samples/doom', '/app0');

kyty_load_elf('/app0/doom/x64/Debug/doom.oelf');
kyty_load_elf('/app0/sce_module/libc.prx', 0);
kyty_load_elf('/app0/sce_module/libSceFios2.prx', 0);

kyty_load_symbols('libc_internal_1');
kyty_load_symbols('libKeyboard_1');
kyty_load_symbols('libkernel_1');
kyty_load_symbols('libSysmodule_1');
kyty_load_symbols('libSystemService_1');
kyty_load_symbols('libUserService_1');
kyty_load_symbols('libVideoOut_1');

kyty_execute();
