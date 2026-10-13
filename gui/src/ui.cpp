#include <SDL2/SDL.h>

#include "logger.h"
#include "ui.h"


#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "ui.h"
#include "QtreGB.h" // your emulator

using namespace qtreGB;

// Global IDs (you can put them in your class/emulator)
GLuint quadVAO = 0;
GLuint quadVBO = 0;
GLuint shaderProgram = 0;


static int contador = 0;

SDL_Window *sdlWindow = nullptr;
SDL_Renderer *sdlRenderer = nullptr;
SDL_Texture *sdlTexture = nullptr;
SDL_Surface *screen = nullptr;

SDL_Window *sdlDebugWindow;
SDL_Renderer *sdlDebugRenderer;
SDL_Texture *sdlDebugTexture;
SDL_Surface *debugScreen;

GLuint game_texture;
SDL_GLContext gl_context;

static int scale = 4;
//static u32 prev_frame_time = 0;
//static u32 target_frame_time = 1000 / 60;

using namespace std;

static unsigned long tile_colors[4] = {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};
//static unsigned long tile_colors[4] = {0xFF9bbc0f, 0xFF8bac0f, 0xFF306230, 0xFF0f380f};

GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if(!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        std::cerr << "Shader compile error: " << info << std::endl;
    }
    return shader;
}

// --- Quad and shader initialization ---
void InitQuadShader() {
    if(quadVAO != 0) return; // already initialized

    // 2D quad (-1..1)
    float vertices[] = {
        // positions   // texcoords
        -1.f, -1.f,   0.f, 0.f,
         1.f, -1.f,   1.f, 0.f,
         1.f,  1.f,   1.f, 1.f,

        -1.f, -1.f,   0.f, 0.f,
         1.f,  1.f,   1.f, 1.f,
        -1.f,  1.f,   0.f, 1.f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);

    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Shaders
    const char* vertexSrc = R"(#version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTex;
        out vec2 TexCoords;
        void main() {
            TexCoords = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    const char* fragmentSrc = R"(#version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoords);
        }
    )";

    GLuint vert = CompileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vert);
    glAttachShader(shaderProgram, frag);
    glLinkProgram(shaderProgram);

    GLint success;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if(!success) std::cerr << "Shader link error" << std::endl;

    glDeleteShader(vert);
    glDeleteShader(frag);
}

// --- Render the quad with texture ---
void RenderQuadWithTexture(GLuint texture) {
    InitQuadShader();

    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}


void ui_init()
{
	SDL_Init(SDL_INIT_VIDEO);

	SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &sdlWindow, &sdlRenderer);
	SDL_CreateWindowAndRenderer(16 * 8 * scale, 32 * 8 * scale, 0, &sdlDebugWindow, &sdlDebugRenderer);

	screen = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
									0x00FF0000,
									0x0000FF00,
									0x000000FF,
									0xFF000000);
	sdlTexture = SDL_CreateTexture(sdlRenderer,
									SDL_PIXELFORMAT_ARGB8888,
									SDL_TEXTUREACCESS_STREAMING,
									SCREEN_WIDTH, SCREEN_HEIGHT);



	debugScreen = SDL_CreateRGBSurface(0, (16 * 8 * scale) + (16 * scale),
											(32 * 8 * scale) + (64 * scale), 32,
											0x00FF0000,
											0x0000FF00,
											0x000000FF,
											0xFF000000);

	sdlDebugTexture = SDL_CreateTexture(sdlDebugRenderer,
											SDL_PIXELFORMAT_ARGB8888,
											SDL_TEXTUREACCESS_STREAMING,
											(16 * 8 * scale) + (16 * scale),
											(32 * 8 * scale) + (64 * scale));

	int x, y;

	SDL_GetWindowPosition(sdlWindow, &x, &y);
	SDL_SetWindowPosition(sdlDebugWindow, x + SCREEN_WIDTH + 10, y);
}



void display_tile(QtreGB *emulator, SDL_Surface *surface, u16 startLocation, u16 tileNum, int x, int y)
{
	SDL_Rect rc;

	for (int tileY=0; tileY<16; tileY += 2)
	{
		u8 b1 = emulator->m_bus.read(startLocation + (tileNum * 16) + tileY);
		u8 b2 = emulator->m_bus.read(startLocation + (tileNum * 16) + tileY + 1);

		for (int bit=7; bit >= 0; bit--)
		{
			u8 hi = !!(b1 & (1 << bit)) << 1;
			u8 lo = !!(b2 & (1 << bit));

			u8 color = hi | lo;

			rc.x = x + ((7 - bit) * scale);
			rc.y = y + (tileY / 2 * scale);
			rc.w = scale;
			rc.h = scale;

			SDL_FillRect(surface, &rc, tile_colors[color]);
		}
	}
}


void update_dbg_window(QtreGB *emulator)
{
    int xDraw = 0;
    int yDraw = 0;
    int tileNum = 0;

    SDL_Rect rc;
    rc.x = 0;
    rc.y = 0;
    rc.w = debugScreen->w;
    rc.h = debugScreen->h;
    SDL_FillRect(debugScreen, &rc, 0xFF111111);

    u16 addr = 0x8000;

    //384 tiles, 24 x 16
    for (int y=0; y<24; y++) {
        for (int x=0; x<16; x++) {
            display_tile(emulator, debugScreen, addr, tileNum, xDraw + (x * scale), yDraw + (y * scale));
            xDraw += (8 * scale);
            tileNum++;
        }

        yDraw += (8 * scale);
        xDraw = 0;
    }

	SDL_UpdateTexture(sdlDebugTexture, NULL, debugScreen->pixels, debugScreen->pitch);
	SDL_RenderClear(sdlDebugRenderer);
	SDL_RenderCopy(sdlDebugRenderer, sdlDebugTexture, NULL, NULL);
	SDL_RenderPresent(sdlDebugRenderer);
}

void update_main_window(QtreGB *emulator)
{
	SDL_Rect rc;
    rc.x = rc.y = 0;
    rc.w = rc.h = 2048;

	std::vector<u8> video_buffer = emulator->m_ppu.get_video_buffer();
	std::vector<u32> video_buffer_color = std::vector<u32>(emulator->m_ppu.get_video_buffer().size());
	for(size_t i = 0; i < emulator->m_ppu.get_video_buffer().size(); ++i)
	{
		video_buffer_color[i] = tile_colors[video_buffer[i]];
	}
	std::fill(video_buffer.begin(), video_buffer.end(), 0);

	for (int line_num = 0; line_num < PPU::YRES; line_num++)
	{
		for (int x = 0; x < PPU::XRES; x++)
		{
			rc.x = x * scale;
			rc.y = line_num * scale;
			rc.w = scale;
			rc.h = scale;

			SDL_FillRect(screen, &rc, video_buffer_color[x + (line_num * PPU::XRES)]);
		}
	}

	SDL_UpdateTexture(sdlTexture, NULL, screen->pixels, screen->pitch);
	SDL_RenderClear(sdlRenderer);
	SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
	SDL_RenderPresent(sdlRenderer);
}

void ui_update(QtreGB *emulator)
{
	contador++;
	if(emulator->get_frame_ready())
	{
		LOG("FRAME READY", "===============================================================================");
		contador = 0;
		update_dbg_window(emulator);
		update_main_window(emulator);
/*
		u32 end = SDL_GetTicks();
		u32 frame_time = end - prev_frame_time;

		if(frame_time < target_frame_time)
		{
			SDL_Delay(target_frame_time - frame_time);
		}

		//printf("target time: %d, frame time: %d\n", target_frame_time, frame_time);
		prev_frame_time = SDL_GetTicks();

		//active_LOG(false);

		ui_handle_events(emulator);
*/
	}
}

void ui_on_key(QtreGB *emulator, bool down, u32 key_code)
{

	switch(key_code)
	{
		case SDLK_z: emulator->m_gamepad.m_button_b = down; break;
		case SDLK_x: emulator->m_gamepad.m_button_a = down; break;
		case SDLK_RETURN: emulator->m_gamepad.m_button_start = down; break;
		case SDLK_TAB: emulator->m_gamepad.m_button_select = down; break;
		case SDLK_UP: emulator->m_gamepad.m_button_up = down; break;
		case SDLK_DOWN: emulator->m_gamepad.m_button_down = down; break;
		case SDLK_LEFT: emulator->m_gamepad.m_button_left = down; break;
		case SDLK_RIGHT: emulator->m_gamepad.m_button_right = down; break;
		case SDLK_p:
			if(down)
			{
				active_LOG(true);
			}
			break;
	}
}

void ui_handle_events(QtreGB *emulator)
{
	SDL_Event e;
	while (SDL_PollEvent(&e) > 0)
	{
		if (e.type == SDL_KEYDOWN) {
			ui_on_key(emulator, true, e.key.keysym.sym);
		}

		if (e.type == SDL_KEYUP) {
			ui_on_key(emulator, false, e.key.keysym.sym);
		}

		if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)
		{

			#ifdef ENABLE_LOG
				Logger::instance().dump(); // prints CSV
			#endif
			exit(-1);
		}
	}
}


static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
//static SDL_Surface* screen = nullptr;




void ui_init_imgui()
{
    if(SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "Error SDL_Init: " << SDL_GetError() << std::endl;
        exit(-1);
    }

    window = SDL_CreateWindow("Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              PPU::XRES * scale, PPU::YRES * scale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if(!window)
    {
        std::cerr << "Error SDL_CreateWindow: " << SDL_GetError() << std::endl;
        exit(-1);
    }

    // Create SDL_Renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!renderer)
    {
        std::cerr << "Error SDL_CreateRenderer: " << SDL_GetError() << std::endl;
        exit(-1);
    }

    // Create SDL_Texture for the emulator framebuffer
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STREAMING,
                                PPU::XRES, PPU::YRES);
    if(!texture)
    {
        std::cerr << "Error SDL_CreateTexture: " << SDL_GetError() << std::endl;
        exit(-1);
    }

    // Initialize ImGui with the SDL_Renderer backend
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);  // uses the SDL_Renderer2 backend
    ImGui::StyleColorsDark();
}

void ui_render(QtreGB* emu)
{
    // --- Framerate control ---
    static u32 prev_frame_time = SDL_GetTicks();
    const u32 target_frame_time = 1000 / 60;

    if (!emu->get_frame_ready()) return;

    u32 end = SDL_GetTicks();
    u32 frame_time = end - prev_frame_time;
    if(frame_time < target_frame_time)
	{
        SDL_Delay(target_frame_time - frame_time);
	}
    prev_frame_time = SDL_GetTicks();

    // --- Process SDL events ---
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if(event.type == SDL_QUIT) exit(0);
        if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            ui_on_key(emu, event.type == SDL_KEYDOWN, event.key.keysym.sym);
    }

	ImGuiIO& io = ImGui::GetIO(); (void)io;

    // --- Update the OpenGL texture with the emulator framebuffer ---
    std::vector<u8> video_buffer = emu->m_ppu.get_video_buffer();
    std::vector<u32> video_buffer_color(video_buffer.size());
    for(size_t i = 0; i < video_buffer.size(); ++i)
        video_buffer_color[i] = tile_colors[video_buffer[i]];

    // --- Begin ImGui frame ---
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    // --- Main window + menu ---
    ImGui::Begin("Main Window", nullptr, window_flags);
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open...")) {
                // Function to open ROM
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

	// Create DockSpace
	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0,0), ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::End();
	ImGui::PopStyleVar(2);

    ImGui::Begin("Screen");
	SDL_UpdateTexture(texture, NULL, video_buffer_color.data(), PPU::XRES * sizeof(u32));
	ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(160*4, 144*4));
	ImGui::End();

    // --- SDL render ---
    SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
    SDL_RenderClear(renderer);

    // Render ImGui
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

    SDL_RenderPresent(renderer);
}

void ui_shutdown()
{

    ImGui::DestroyContext();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);

    //SDL_DestroyTexture(texture);
    //SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
