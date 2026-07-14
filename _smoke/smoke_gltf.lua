local cfg = { ScreenWidth=1920; ScreenHeight=1080; Neo=true; VulkanValidationEnabled=false;
  ShaderValidationEnabled=false; ShaderOptimizationType='Performance'; ShaderLogDirection='Console';
  ShaderLogFolder='_Shaders'; CommandBufferDumpEnabled=false; CommandBufferDumpFolder='_Buffers';
  PrintfDirection='Console'; PrintfOutputFile='_kyty.txt'; ProfilerDirection='None'; ProfilerOutputFile='_profile.prof'; }
kyty_init(cfg);
kyty_mount('/home/brice/Projects/kyty-assets/freegnm-examples/gltf', '/app0');
kyty_load_elf('/app0/gltf.oelf');
kyty_load_symbols('libc_internal_1'); kyty_load_symbols('libkernel_1');
kyty_load_symbols('libGraphicsDriver_1'); kyty_load_symbols('libVideoOut_1');
kyty_execute();
