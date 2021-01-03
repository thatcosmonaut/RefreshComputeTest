/* this implementation is heavily borrowed from Sascha Willem's computeparticles example:
 * https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeparticles/computeparticles.cpp
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Refresh.h>
#include <Refresh_Image.h>

#define PARTICLE_COUNT 256 * 1024

typedef struct Particle
{
	float xPosition, yPosition;
	float xVelocity, yVelocity;
	float gradientPosition, dummy1, dummy2, dummy3;
} Particle;

typedef struct ParticleComputeUniforms
{
	float deltaTime;
	float destinationX, destinationY;
	uint32_t particleCount;
} ParticleComputeUniforms;

float randomFloat(float min, float max)
{
    float scale = rand() / (float) RAND_MAX; /* [0, 1.0] */
    return min + scale * ( max - min );      /* [min, max] */
}

int main(int argc, char *argv[])
{
	uint32_t i;

	srand((uint32_t)time(NULL));

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL\n\t%s\n", SDL_GetError());
		return -1;
	}

	const int windowWidth = 1280;
	const int windowHeight = 720;

	SDL_Window *window = SDL_CreateWindow(
		"Refresh Compute Test",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowWidth,
		windowHeight,
		SDL_WINDOW_VULKAN
	);

	REFRESH_PresentationParameters presentationParameters;
	presentationParameters.deviceWindowHandle = window;
	presentationParameters.presentMode = REFRESH_PRESENTMODE_IMMEDIATE;

	REFRESH_Device *device = REFRESH_CreateDevice(&presentationParameters, 1);

	bool quit = false;

	double t = 0.0;
	double dt = 0.01;

	uint64_t currentTime = SDL_GetPerformanceCounter();
	double accumulator = 0.0;

	REFRESH_Rect renderArea;
	renderArea.x = 0;
	renderArea.y = 0;
	renderArea.w = windowWidth;
	renderArea.h = windowHeight;

	/* Compile shaders */

	SDL_RWops* file = SDL_RWFromFile("particle.vert.spv", "rb");
	Sint64 shaderCodeSize = SDL_RWsize(file);
	uint32_t* byteCode = SDL_malloc(sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo particleVertexShaderModuleCreateInfo;
	particleVertexShaderModuleCreateInfo.byteCode = byteCode;
	particleVertexShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule* particleVertexShaderModule = REFRESH_CreateShaderModule(device, &particleVertexShaderModuleCreateInfo);

	file = SDL_RWFromFile("particle.frag.spv", "rb");
	shaderCodeSize = SDL_RWsize(file);
	byteCode = SDL_realloc(byteCode, sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo particleFragmentShaderModuleCreateInfo;
	particleFragmentShaderModuleCreateInfo.byteCode = byteCode;
	particleFragmentShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule* particleFragmentShaderModule = REFRESH_CreateShaderModule(device, &particleFragmentShaderModuleCreateInfo);

	file = SDL_RWFromFile("particle.comp.spv", "rb");
	shaderCodeSize = SDL_RWsize(file);
	byteCode = SDL_realloc(byteCode, sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo particleComputeShaderModuleCreateInfo;
	particleComputeShaderModuleCreateInfo.byteCode = byteCode;
	particleComputeShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule *particleComputeShaderModule = REFRESH_CreateShaderModule(device, &particleComputeShaderModuleCreateInfo);

	SDL_free(byteCode);

	/* Load textures */

	int32_t textureWidth, textureHeight, numChannels;
	uint8_t *particleTexturePixels = REFRESH_Image_Load(
		"particle01_rgba.png",
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	REFRESH_Texture *particleTexture = REFRESH_CreateTexture2D(
		device,
		REFRESH_SURFACEFORMAT_R8G8B8A8,
		textureWidth,
		textureHeight,
		1,
		REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	REFRESH_TextureSlice setTextureSlice;
	setTextureSlice.texture = particleTexture;
	setTextureSlice.rectangle.x = 0;
	setTextureSlice.rectangle.y = 0;
	setTextureSlice.rectangle.w = textureWidth;
	setTextureSlice.rectangle.h = textureHeight;
	setTextureSlice.depth = 0;
	setTextureSlice.layer = 0;
	setTextureSlice.level = 0;

	REFRESH_SetTextureData(
		device,
		&setTextureSlice,
		particleTexturePixels,
		textureWidth * textureHeight * 4
	);

	REFRESH_Image_Free(particleTexturePixels);

	uint8_t *particleGradientTexturePixels = REFRESH_Image_Load(
		"particle_gradient_rgba.png",
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	REFRESH_Texture *particleGradientTexture = REFRESH_CreateTexture2D(
		device,
		REFRESH_SURFACEFORMAT_R8G8B8A8,
		textureWidth,
		textureHeight,
		1,
		REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	setTextureSlice.texture = particleGradientTexture;
	setTextureSlice.rectangle.x = 0;
	setTextureSlice.rectangle.y = 0;
	setTextureSlice.rectangle.w = textureWidth;
	setTextureSlice.rectangle.h = textureHeight;
	setTextureSlice.depth = 0;
	setTextureSlice.layer = 0;
	setTextureSlice.level = 0;

	REFRESH_SetTextureData(
		device,
		&setTextureSlice,
		particleGradientTexturePixels,
		textureWidth * textureHeight * 4
	);

	REFRESH_Image_Free(particleGradientTexturePixels);

	/* Define vertex buffer */

	Particle *particles = SDL_malloc(sizeof(Particle) * PARTICLE_COUNT);

	for (i = 0; i < PARTICLE_COUNT; i += 1)
	{
		particles[i].xPosition = randomFloat(-1, 1);
		particles[i].yPosition = randomFloat(-1, 1);
		particles[i].xVelocity = 0;
		particles[i].yVelocity = 1;
		particles[i].gradientPosition = particles[i].xPosition / 2.0f;
		particles[i].dummy1 = 0;
		particles[i].dummy2 = 0;
		particles[i].dummy3 = 0;
	}

	REFRESH_Buffer* particleBuffer = REFRESH_CreateBuffer(
		device,
		REFRESH_BUFFERUSAGE_VERTEX_BIT | REFRESH_BUFFERUSAGE_COMPUTE_BIT,
		sizeof(Particle) * PARTICLE_COUNT
	);

	REFRESH_SetBufferData(device, particleBuffer, 0, particles, sizeof(Particle) * PARTICLE_COUNT);

	uint64_t* offsets = SDL_malloc(sizeof(uint64_t));
	offsets[0] = 0;

	/* Define RenderPass */

	REFRESH_ColorTargetDescription mainColorTargetDescription;
	mainColorTargetDescription.format = REFRESH_SURFACEFORMAT_R8G8B8A8;
	mainColorTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainColorTargetDescription.storeOp = REFRESH_STOREOP_STORE;
	mainColorTargetDescription.multisampleCount = REFRESH_SAMPLECOUNT_1;

	REFRESH_DepthStencilTargetDescription mainDepthStencilTargetDescription;
	mainDepthStencilTargetDescription.depthFormat = REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT;
	mainDepthStencilTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainDepthStencilTargetDescription.storeOp = REFRESH_STOREOP_DONT_CARE;
	mainDepthStencilTargetDescription.stencilLoadOp = REFRESH_LOADOP_DONT_CARE;
	mainDepthStencilTargetDescription.stencilStoreOp = REFRESH_STOREOP_DONT_CARE;

	REFRESH_RenderPassCreateInfo mainRenderPassCreateInfo;
	mainRenderPassCreateInfo.colorTargetCount = 1;
	mainRenderPassCreateInfo.colorTargetDescriptions = &mainColorTargetDescription;
	mainRenderPassCreateInfo.depthTargetDescription = &mainDepthStencilTargetDescription;

	REFRESH_RenderPass *mainRenderPass = REFRESH_CreateRenderPass(device, &mainRenderPassCreateInfo);

	/* Define ColorTarget */

	REFRESH_Texture *mainColorTargetTexture = REFRESH_CreateTexture2D(
		device,
		REFRESH_SURFACEFORMAT_R8G8B8A8,
		windowWidth,
		windowHeight,
		1,
		REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT
	);

	REFRESH_TextureSlice mainColorTargetTextureSlice;
	mainColorTargetTextureSlice.texture = mainColorTargetTexture;
	mainColorTargetTextureSlice.rectangle.x = 0;
	mainColorTargetTextureSlice.rectangle.y = 0;
	mainColorTargetTextureSlice.rectangle.w = windowWidth;
	mainColorTargetTextureSlice.rectangle.h = windowHeight;
	mainColorTargetTextureSlice.depth = 0;
	mainColorTargetTextureSlice.layer = 0;
	mainColorTargetTextureSlice.level = 0;

	REFRESH_ColorTarget *mainColorTarget = REFRESH_CreateColorTarget(
		device,
		REFRESH_SAMPLECOUNT_1,
		&mainColorTargetTextureSlice
	);

	REFRESH_DepthStencilTarget *mainDepthStencilTarget = REFRESH_CreateDepthStencilTarget(
		device,
		windowWidth,
		windowHeight,
		REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT
	);

	/* Define Framebuffer */

	REFRESH_FramebufferCreateInfo framebufferCreateInfo;
	framebufferCreateInfo.width = 1280;
	framebufferCreateInfo.height = 720;
	framebufferCreateInfo.colorTargetCount = 1;
	framebufferCreateInfo.pColorTargets = &mainColorTarget;
	framebufferCreateInfo.pDepthStencilTarget = mainDepthStencilTarget;
	framebufferCreateInfo.renderPass = mainRenderPass;

	REFRESH_Framebuffer *mainFramebuffer = REFRESH_CreateFramebuffer(device, &framebufferCreateInfo);

	/* Define pipeline */
	REFRESH_ColorTargetBlendState renderTargetBlendState;
	renderTargetBlendState.blendEnable = 1;
	renderTargetBlendState.alphaBlendOp = REFRESH_BLENDOP_ADD;
	renderTargetBlendState.colorBlendOp = REFRESH_BLENDOP_ADD;
	renderTargetBlendState.colorWriteMask =
		REFRESH_COLORCOMPONENT_R_BIT |
		REFRESH_COLORCOMPONENT_G_BIT |
		REFRESH_COLORCOMPONENT_B_BIT |
		REFRESH_COLORCOMPONENT_A_BIT;
	renderTargetBlendState.dstAlphaBlendFactor = REFRESH_BLENDFACTOR_DST_ALPHA;
	renderTargetBlendState.dstColorBlendFactor = REFRESH_BLENDFACTOR_ONE;
	renderTargetBlendState.srcAlphaBlendFactor = REFRESH_BLENDFACTOR_SRC_ALPHA;
	renderTargetBlendState.srcColorBlendFactor = REFRESH_BLENDFACTOR_ONE;

	REFRESH_ColorBlendState colorBlendState;
	colorBlendState.logicOpEnable = 0;
	colorBlendState.blendConstants[0] = 0.0f;
	colorBlendState.blendConstants[1] = 0.0f;
	colorBlendState.blendConstants[2] = 0.0f;
	colorBlendState.blendConstants[3] = 0.0f;
	colorBlendState.blendStateCount = 1;
	colorBlendState.blendStates = &renderTargetBlendState;
	colorBlendState.logicOp = REFRESH_LOGICOP_NO_OP;

	REFRESH_DepthStencilState depthStencilState;
	depthStencilState.depthTestEnable = 0;
	depthStencilState.backStencilState.compareMask = 0;
	depthStencilState.backStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.backStencilState.depthFailOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.failOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.passOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.reference = 0;
	depthStencilState.backStencilState.writeMask = 0;
	depthStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.depthBoundsTestEnable = 0;
	depthStencilState.depthWriteEnable = 0;
	depthStencilState.frontStencilState.compareMask = 0;
	depthStencilState.frontStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.frontStencilState.depthFailOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.failOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.passOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.reference = 0;
	depthStencilState.frontStencilState.writeMask = 0;
	depthStencilState.maxDepthBounds = 1.0f;
	depthStencilState.minDepthBounds = 0.0f;
	depthStencilState.stencilTestEnable = 0;

	REFRESH_ShaderStageState vertexShaderStageState;
	vertexShaderStageState.shaderModule = particleVertexShaderModule;
	vertexShaderStageState.entryPointName = "main";
	vertexShaderStageState.uniformBufferSize = 0;

	REFRESH_ShaderStageState fragmentShaderStageState;
	fragmentShaderStageState.shaderModule = particleFragmentShaderModule;
	fragmentShaderStageState.entryPointName = "main";
	fragmentShaderStageState.uniformBufferSize = 0;

	REFRESH_ShaderStageState computeShaderStageState;
	computeShaderStageState.shaderModule = particleComputeShaderModule;
	computeShaderStageState.entryPointName = "main";
	computeShaderStageState.uniformBufferSize = sizeof(ParticleComputeUniforms);

	REFRESH_MultisampleState multisampleState;
	multisampleState.multisampleCount = REFRESH_SAMPLECOUNT_1;
	multisampleState.sampleMask = 0;

	REFRESH_GraphicsPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	pipelineLayoutCreateInfo.vertexSamplerBindingCount = 0;
	pipelineLayoutCreateInfo.fragmentSamplerBindingCount = 2;

	REFRESH_RasterizerState rasterizerState;
	rasterizerState.cullMode = REFRESH_CULLMODE_NONE;
	rasterizerState.depthBiasClamp = 0;
	rasterizerState.depthBiasConstantFactor = 0;
	rasterizerState.depthBiasEnable = 0;
	rasterizerState.depthBiasSlopeFactor = 0;
	rasterizerState.depthClampEnable = 0;
	rasterizerState.fillMode = REFRESH_FILLMODE_FILL;
	rasterizerState.frontFace = REFRESH_FRONTFACE_CLOCKWISE;
	rasterizerState.lineWidth = 1.0f;

	REFRESH_TopologyState topologyState;
	topologyState.topology = REFRESH_PRIMITIVETYPE_POINTLIST;

	REFRESH_VertexBinding vertexBinding;
	vertexBinding.binding = 0;
	vertexBinding.inputRate = REFRESH_VERTEXINPUTRATE_VERTEX;
	vertexBinding.stride = sizeof(Particle);

	REFRESH_VertexAttribute *vertexAttributes = SDL_stack_alloc(REFRESH_VertexAttribute, 2);
	vertexAttributes[0].binding = 0;
	vertexAttributes[0].location = 0;
	vertexAttributes[0].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR2;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 0;
	vertexAttributes[1].location = 1;
	vertexAttributes[1].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR4;
	vertexAttributes[1].offset = sizeof(float) * 4;

	REFRESH_VertexInputState vertexInputState;
	vertexInputState.vertexBindings = &vertexBinding;
	vertexInputState.vertexBindingCount = 1;
	vertexInputState.vertexAttributes = vertexAttributes;
	vertexInputState.vertexAttributeCount = 2;

	REFRESH_Viewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.w = (float)windowWidth;
	viewport.h = (float)windowHeight;
	viewport.minDepth = 0;
	viewport.maxDepth = 1;

	REFRESH_ViewportState viewportState;
	viewportState.viewports = &viewport;
	viewportState.viewportCount = 1;
	viewportState.scissors = &renderArea;
	viewportState.scissorCount = 1;

	REFRESH_GraphicsPipelineCreateInfo graphicsPipelineCreateInfo;
	graphicsPipelineCreateInfo.colorBlendState = colorBlendState;
	graphicsPipelineCreateInfo.depthStencilState = depthStencilState;
	graphicsPipelineCreateInfo.vertexShaderState = vertexShaderStageState;
	graphicsPipelineCreateInfo.fragmentShaderState = fragmentShaderStageState;
	graphicsPipelineCreateInfo.multisampleState = multisampleState;
	graphicsPipelineCreateInfo.pipelineLayoutCreateInfo = pipelineLayoutCreateInfo;
	graphicsPipelineCreateInfo.rasterizerState = rasterizerState;
	graphicsPipelineCreateInfo.topologyState = topologyState;
	graphicsPipelineCreateInfo.vertexInputState = vertexInputState;
	graphicsPipelineCreateInfo.viewportState = viewportState;
	graphicsPipelineCreateInfo.renderPass = mainRenderPass;

	REFRESH_GraphicsPipeline* graphicsPipeline = REFRESH_CreateGraphicsPipeline(device, &graphicsPipelineCreateInfo);

	REFRESH_ComputePipelineLayoutCreateInfo computePipelineLayoutCreateInfo;
	computePipelineLayoutCreateInfo.bufferBindingCount = 1;
	computePipelineLayoutCreateInfo.imageBindingCount = 0;

	REFRESH_ComputePipelineCreateInfo computePipelineCreateInfo;
	computePipelineCreateInfo.computeShaderState = computeShaderStageState;
	computePipelineCreateInfo.pipelineLayoutCreateInfo = computePipelineLayoutCreateInfo;

	REFRESH_ComputePipeline *computePipeline = REFRESH_CreateComputePipeline(device, &computePipelineCreateInfo);

	REFRESH_Color clearColor;
	clearColor.r = 0;
	clearColor.g = 0;
	clearColor.b = 0;
	clearColor.a = 255;

	REFRESH_DepthStencilValue depthStencilClear;
	depthStencilClear.depth = 1.0f;
	depthStencilClear.stencil = 0;

	/* Sampling */

	REFRESH_SamplerStateCreateInfo samplerStateCreateInfo;
	samplerStateCreateInfo.addressModeU = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.addressModeV = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.addressModeW = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.anisotropyEnable = 0;
	samplerStateCreateInfo.borderColor = REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK;
	samplerStateCreateInfo.compareEnable = 0;
	samplerStateCreateInfo.compareOp = REFRESH_COMPAREOP_NEVER;
	samplerStateCreateInfo.magFilter = REFRESH_FILTER_LINEAR;
	samplerStateCreateInfo.maxAnisotropy = 0;
	samplerStateCreateInfo.maxLod = 1;
	samplerStateCreateInfo.minFilter = REFRESH_FILTER_LINEAR;
	samplerStateCreateInfo.minLod = 1;
	samplerStateCreateInfo.mipLodBias = 1;
	samplerStateCreateInfo.mipmapMode = REFRESH_SAMPLERMIPMAPMODE_LINEAR;

	REFRESH_Sampler *sampler = REFRESH_CreateSampler(
		device,
		&samplerStateCreateInfo
	);

	REFRESH_Texture* sampleTextures[2];
	sampleTextures[0] = particleTexture;
	sampleTextures[1] = particleGradientTexture;

	REFRESH_Sampler* sampleSamplers[2];
	sampleSamplers[0] = sampler;
	sampleSamplers[1] = sampler;

	uint8_t screenshotKey = 0;
	uint8_t *screenshotPixels = SDL_malloc(sizeof(uint8_t) * windowWidth * windowHeight * 4);
	REFRESH_Buffer *screenshotBuffer = REFRESH_CreateBuffer(device, 0, windowWidth * windowHeight * 4);

	ParticleComputeUniforms particleComputeUniforms;
	particleComputeUniforms.particleCount = PARTICLE_COUNT;

	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				quit = true;
				break;
			}
		}

		uint64_t newTime = SDL_GetPerformanceCounter();
		double frameTime = (newTime - currentTime) / (double)SDL_GetPerformanceFrequency();

		if (frameTime > 0.25)
			frameTime = 0.25;
		currentTime = newTime;

		accumulator += frameTime;

		bool updateThisLoop = (accumulator >= dt);

		while (accumulator >= dt && !quit)
		{
			// Update here!

			t += dt;
			accumulator -= dt;

			const uint8_t *keyboardState = SDL_GetKeyboardState(NULL);

			if (keyboardState[SDL_SCANCODE_S])
			{
				if (screenshotKey == 1)
				{
					screenshotKey = 2;
				}
				else
				{
					screenshotKey = 1;
				}
			}
			else
			{
				screenshotKey = 0;
			}
		}

		if (updateThisLoop && !quit)
		{
			// Draw here!

			particleComputeUniforms.deltaTime = (float)dt * 0.25f;
			particleComputeUniforms.destinationX = (float)SDL_sin((t)) * 0.75f;
			particleComputeUniforms.destinationY = 0.0f;

			REFRESH_CommandBuffer *commandBuffer = REFRESH_AcquireCommandBuffer(device, 0);

			REFRESH_BindComputePipeline(device, commandBuffer, computePipeline);
			REFRESH_BindComputeBuffers(device, commandBuffer, &particleBuffer);
			uint32_t computeParamOffset = REFRESH_PushComputeShaderParams(device, commandBuffer, &particleComputeUniforms, 1);
			REFRESH_DispatchCompute(device, commandBuffer, PARTICLE_COUNT / 256, 1, 1, computeParamOffset);

			REFRESH_BeginRenderPass(
				device,
				commandBuffer,
				mainRenderPass,
				mainFramebuffer,
				renderArea,
				&clearColor,
				1,
				&depthStencilClear
			);

			REFRESH_BindGraphicsPipeline(
				device,
				commandBuffer,
				graphicsPipeline
			);

			REFRESH_BindVertexBuffers(device, commandBuffer, 0, 1, &particleBuffer, offsets);
			REFRESH_SetFragmentSamplers(device, commandBuffer, sampleTextures, sampleSamplers);
			REFRESH_DrawPrimitives(device, commandBuffer, 0, PARTICLE_COUNT, 0, 0);

			REFRESH_EndRenderPass(device, commandBuffer);

			if (screenshotKey == 1)
			{
				SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "screenshot!");
				REFRESH_CopyTextureToBuffer(device, commandBuffer, &mainColorTargetTextureSlice, screenshotBuffer);
			}

			REFRESH_QueuePresent(device, commandBuffer, &mainColorTargetTextureSlice, &renderArea, REFRESH_FILTER_NEAREST);
			REFRESH_Submit(device, &commandBuffer, 1);

			/* FIXME: sync */
			if (screenshotKey == 1)
			{
				REFRESH_Wait(device);
				REFRESH_GetBufferData(device, screenshotBuffer, screenshotPixels, windowWidth * windowHeight * 4);
				REFRESH_Image_SavePNG("screenshot.png", windowWidth, windowHeight, screenshotPixels);
			}
		}
	}

	SDL_free(screenshotPixels);

	REFRESH_AddDisposeColorTarget(device, mainColorTarget);
	REFRESH_AddDisposeDepthStencilTarget(device, mainDepthStencilTarget);

	REFRESH_AddDisposeTexture(device, particleTexture);
	REFRESH_AddDisposeTexture(device, particleGradientTexture);
	REFRESH_AddDisposeTexture(device, mainColorTargetTexture);
	REFRESH_AddDisposeSampler(device, sampler);

	REFRESH_AddDisposeBuffer(device, screenshotBuffer);
	REFRESH_AddDisposeBuffer(device, particleBuffer);

	REFRESH_AddDisposeGraphicsPipeline(device, graphicsPipeline);
	REFRESH_AddDisposeComputePipeline(device, computePipeline);

	REFRESH_AddDisposeShaderModule(device, particleVertexShaderModule);
	REFRESH_AddDisposeShaderModule(device, particleFragmentShaderModule);
	REFRESH_AddDisposeShaderModule(device, particleComputeShaderModule);

	REFRESH_AddDisposeFramebuffer(device, mainFramebuffer);
	REFRESH_AddDisposeRenderPass(device, mainRenderPass);

	REFRESH_DestroyDevice(device);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
