local cfg = {
	ScreenWidth = 1280;
	ScreenHeight = 720;
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

kyty_mount('/home/brice/Projects/kyty-assets/openorbis/OpenOrbis/PS4Toolchain/samples/pngdec', '/app0');

kyty_load_elf('/app0/pngdec/x64/Debug/pngdec.oelf');
kyty_load_elf('/app0/sce_module/libc.prx', 0);
kyty_load_elf('/app0/sce_module/libSceFios2.prx', 0);

kyty_load_symbols('libAudio_1');
kyty_load_symbols('libc_internal_1');
kyty_load_symbols('libDebug_1');
kyty_load_symbols('libDialog_1');
kyty_load_symbols('libDiscMap_1');
kyty_load_symbols('libFreeType_1');
kyty_load_symbols('libGraphicsDriver_1');
kyty_load_symbols('libkernel_1');
kyty_load_symbols('libNet_1');
kyty_load_symbols('libPad_1');
kyty_load_symbols('libPlayGo_1');
kyty_load_symbols('libSysmodule_1');
kyty_load_symbols('libSystemService_1');
kyty_load_symbols('libUserService_1');
kyty_load_symbols('libVideoOut_1');

kyty_execute();
