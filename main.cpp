#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <ctime>
#include <algorithm>

bool game_running = false;
int move_dir = 0;
bool fire_pressed = 0;

constexpr size_t buffer_width = 600;
constexpr size_t buffer_height = 400;
constexpr size_t NUM_OF_ALIEN_ROWS = 6;
constexpr size_t NUM_OF_ALIEN_TYPES = 3;
#define GL_ERROR_CASE(glerror)\
    case glerror: snprintf(error, sizeof(error), "%s", #glerror)

inline void gl_debug(const char *file, int line) {
    GLenum err;
    while((err = glGetError()) != GL_NO_ERROR){
        char error[128];

        switch(err) {
            GL_ERROR_CASE(GL_INVALID_ENUM); break;
            GL_ERROR_CASE(GL_INVALID_VALUE); break;
            GL_ERROR_CASE(GL_INVALID_OPERATION); break;
            GL_ERROR_CASE(GL_INVALID_FRAMEBUFFER_OPERATION); break;
            GL_ERROR_CASE(GL_OUT_OF_MEMORY); break;
            default: snprintf(error, sizeof(error), "%s", "UNKNOWN_ERROR"); break;
        }

        fprintf(stderr, "%s - %s: %d\n", error, file, line);
    }
}

#undef GL_ERROR_CASE

void validate_shader(GLuint shader, const char *file = 0){
    static const unsigned int BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE];
    GLsizei length = 0;

    glGetShaderInfoLog(shader, BUFFER_SIZE, &length, buffer);

    if(length>0){
        printf("Shader %d(%s) compile error: %s\n", shader, (file? file: ""), buffer);
    }
}

bool validate_program(GLuint program){
    static constexpr GLsizei BUFFER_SIZE = 512;
    GLchar buffer[BUFFER_SIZE];
    GLsizei length = 0;

    glGetProgramInfoLog(program, BUFFER_SIZE, &length, buffer);

    if(length>0){
        printf("Program %d link error: %s\n", program, buffer);
        return false;
    }

    return true;
}

void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods){
    switch(key){
    case GLFW_KEY_ESCAPE:
        if(action == GLFW_PRESS) game_running = false;
        break;
    case GLFW_KEY_RIGHT:
        if(action == GLFW_PRESS) move_dir += 1;
        else if(action == GLFW_RELEASE) move_dir -= 1;
        break;
    case GLFW_KEY_LEFT:
        if(action == GLFW_PRESS) move_dir -= 1;
        else if(action == GLFW_RELEASE) move_dir += 1;
        break;
    case GLFW_KEY_SPACE:
        if(action == GLFW_RELEASE) fire_pressed = true;
        break;
    default:
        break;
    }
}

struct Buffer
{
    size_t width, height;
    uint32_t* data;
};

struct Sprite
{
    size_t width, height;
    uint8_t* data;
};

struct Sprites{
    Sprite alien_sprites[6];
    Sprite alien_death_sprite;
    Sprite player_sprite;
    Sprite bullet_sprite;
};

struct Alien
{
    size_t x, y;
    uint8_t type;
};

struct Bullet
{
    size_t x, y;
    int dir;
};

struct Player
{
    size_t x, y;
    size_t life;
};

constexpr int GAME_MAX_BULLETS = 128;

struct Game
{
    size_t width, height;
    size_t num_aliens;
    size_t num_bullets;
    Alien* aliens;
    Player player;
    Bullet bullets[GAME_MAX_BULLETS];
};

struct SpriteAnimation
{
    bool loop;
    size_t num_frames;
    size_t frame_duration;
    size_t time;
    Sprite** frames;
};

enum AlienType: uint8_t
{
    ALIEN_DEAD   = 0,
    ALIEN_TYPE_A = 1,
    ALIEN_TYPE_B = 2,
    ALIEN_TYPE_C = 3
};

struct glParams{
    GLFWwindow* window;
    Buffer buffer;
    GLuint fullscreen_triangle_vao;

};
void buffer_clear(Buffer* buffer, uint32_t color)
{
    for(size_t i = 0; i < buffer->width * buffer->height; ++i)
    {
        buffer->data[i] = color;
    }
}

bool sprite_overlap_check(
    const Sprite& sp_a, size_t x_a, size_t y_a,
    const Sprite& sp_b, size_t x_b, size_t y_b
)
{
    if(x_a < x_b + sp_b.width && x_a + sp_a.width > x_b &&
       y_a < y_b + sp_b.height && y_a + sp_a.height > y_b)
    {
        return true;
    }

    return false;
}

void buffer_draw_sprite(Buffer* buffer, const Sprite& sprite, size_t x, size_t y, uint32_t color)
{
    for(size_t xi = 0; xi < sprite.width; ++xi)
    {
        for(size_t yi = 0; yi < sprite.height; ++yi)
        {
            if(sprite.data[yi * sprite.width + xi] &&
               (sprite.height - 1 + y - yi) < buffer->height &&
               (x + xi) < buffer->width)
            {
                buffer->data[(sprite.height - 1 + y - yi) * buffer->width + (x + xi)] = color;
            }
        }
    }
}

uint32_t rgb_to_uint32(uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 24) | (g << 16) | (b << 8) | 255;
}

void update_aliens_position(Game& game)
{
    const int pixels_to_drop = 5;
    const int min_delta_time = 3;
    static std::time_t last_update_time = 0;
    std::time_t delta_time = 0;

    if(last_update_time == 0)
    {
        last_update_time = time(NULL);
    }

    delta_time = time(NULL) - last_update_time;

    if(delta_time > min_delta_time) 
    {
        
        last_update_time = time(NULL);
        
        for(size_t ai = 0; ai < game.num_aliens; ++ai)
        {
            game.aliens[ai].y -= pixels_to_drop;
        }
    }
}

void init_sprites(Sprites& sprites){

    sprites.alien_sprites[0].width = 20;
    sprites.alien_sprites[0].height = 8;
    sprites.alien_sprites[0].data = new uint8_t[64]
    {
        0,0,0,1,1,0,0,0, // ...@@...
        0,0,1,1,1,1,0,0, // ..@@@@..
        0,1,1,1,1,1,1,0, // .@@@@@@.
        1,1,0,1,1,0,1,1, // @@.@@.@@
        1,1,1,1,1,1,1,1, // @@@@@@@@
        0,1,0,1,1,0,1,0, // .@.@@.@.
        1,0,0,0,0,0,0,1, // @......@
        0,1,0,0,0,0,1,0  // .@....@.
    };
    
    sprites.alien_sprites[1].width = 8;
    sprites.alien_sprites[1].height = 8;
    sprites.alien_sprites[1].data = new uint8_t[64]
    {
        0,0,0,1,1,0,0,0, // ...@@...
        0,0,1,1,1,1,0,0, // ..@@@@..
        0,1,1,1,1,1,1,0, // .@@@@@@.
        1,1,0,1,1,0,1,1, // @@.@@.@@
        1,1,1,1,1,1,1,1, // @@@@@@@@
        0,0,1,0,0,1,0,0, // ..@..@..
        0,1,0,1,1,0,1,0, // .@.@@.@.
        1,0,1,0,0,1,0,1  // @.@..@.@
    };
    
    sprites.alien_sprites[2].width = 11;
    sprites.alien_sprites[2].height = 8;
    sprites.alien_sprites[2].data = new uint8_t[88]
    {
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        0,0,0,1,0,0,0,1,0,0,0, // ...@...@...
        0,0,1,1,1,1,1,1,1,0,0, // ..@@@@@@@..
        0,1,1,0,1,1,1,0,1,1,0, // .@@.@@@.@@.
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
        1,0,1,0,0,0,0,0,1,0,1, // @.@.....@.@
        0,0,0,1,1,0,1,1,0,0,0  // ...@@.@@...
    };
    
    sprites.alien_sprites[3].width = 11;
    sprites.alien_sprites[3].height = 8;
    sprites.alien_sprites[3].data = new uint8_t[88]
    {
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        1,0,0,1,0,0,0,1,0,0,1, // @..@...@..@
        1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
        1,1,1,0,1,1,1,0,1,1,1, // @@@.@@@.@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        0,1,0,0,0,0,0,0,0,1,0  // .@.......@.
    };
    
    sprites.alien_sprites[4].width = 12;
    sprites.alien_sprites[4].height = 8;
    sprites.alien_sprites[4].data = new uint8_t[96]
    {
        0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
        0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        0,0,0,1,1,0,0,1,1,0,0,0, // ...@@..@@...
        0,0,1,1,0,1,1,0,1,1,0,0, // ..@@.@@.@@..
        1,1,0,0,0,0,0,0,0,0,1,1  // @@........@@
    };
    
    
    sprites.alien_sprites[5].width = 12;
    sprites.alien_sprites[5].height = 8;
    sprites.alien_sprites[5].data = new uint8_t[96]
    {
        0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
        0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        0,0,1,1,1,0,0,1,1,1,0,0, // ..@@@..@@@..
        0,1,1,0,0,1,1,0,0,1,1,0, // .@@..@@..@@.
        0,0,1,1,0,0,0,0,1,1,0,0  // ..@@....@@..
    };
    
    sprites.alien_death_sprite.width = 13;
    sprites.alien_death_sprite.height = 7;
    sprites.alien_death_sprite.data = new uint8_t[91]
    {
        0,1,0,0,1,0,0,0,1,0,0,1,0, // .@..@...@..@.
        0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
        0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
        1,1,0,0,0,0,0,0,0,0,0,1,1, // @@.........@@
        0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
        0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
        0,1,0,0,1,0,0,0,1,0,0,1,0  // .@..@...@..@.
    };
    
    sprites.player_sprite.width = 11;
    sprites.player_sprite.height = 7;
    sprites.player_sprite.data = new uint8_t[77]
    {
        0,0,0,0,0,1,0,0,0,0,0, // .....@.....
        0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
        0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
        0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
    };
    
    sprites.bullet_sprite.width = 1;
    sprites.bullet_sprite.height = 3;
    sprites.bullet_sprite.data = new uint8_t[3]
    {
        1, // @
        1, // @
        1  // @
    };
}

void init_aliens(Game& game, SpriteAnimation* alien_animation, Sprites& sprites)
{
    for(size_t i = 0; i < 3; ++i)
    {
        alien_animation[i].loop = true;
        alien_animation[i].num_frames = 2;
        alien_animation[i].frame_duration = 10;
        alien_animation[i].time = 0;
        
        alien_animation[i].frames = new Sprite*[2];
        alien_animation[i].frames[0] = &sprites.alien_sprites[2 * i];
        alien_animation[i].frames[1] = &sprites.alien_sprites[2 * i + 1];
    }

    for(size_t yi = 0; yi < NUM_OF_ALIEN_ROWS; ++yi)
    {
        for(size_t xi = 0; xi < 11; ++xi)
        {
            Alien& alien = game.aliens[yi * 11 + xi];
            alien.type = std::min((NUM_OF_ALIEN_ROWS - yi) / 2 + 1,NUM_OF_ALIEN_TYPES);

            const Sprite& sprite = sprites.alien_sprites[2 * (alien.type - 1)];

            alien.x = buffer_width / 11 * xi + 10 + (sprites.alien_death_sprite.width - sprite.width)/2;
            alien.y = 17 * yi + 128;
        }
    }

}

int setup_gl(glParams& params){

    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
    {
        return -1;
    } 

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);


    /* Create a windowed mode window and its OpenGL context */
    params.window = glfwCreateWindow(2 * params.buffer.width, 2 * params.buffer.height, "Space Invaders", NULL, NULL);
    if(!params.window)
    {
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(params.window, key_callback);

    glfwMakeContextCurrent(params.window);

    GLenum err = glewInit();
    if(err != GLEW_OK)
    {
        fprintf(stderr, "Error initializing GLEW.\n");
        glfwTerminate();
        return -1;
    }

    int glVersion[2] = {-1, 1};
    glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]);
    glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]);

    gl_debug(__FILE__, __LINE__);

    printf("Using OpenGL: %d.%d\n", glVersion[0], glVersion[1]);
    printf("Renderer used: %s\n", glGetString(GL_RENDERER));
    printf("Shading Language: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    glfwSwapInterval(1);

    glClearColor(1.0, 0.0, 0.0, 1.0);

    // Create graphics buffer
    

    buffer_clear(&params.buffer, 0);

    // Create texture for presenting buffer to OpenGL
    GLuint buffer_texture;
    glGenTextures(1, &buffer_texture);
    glBindTexture(GL_TEXTURE_2D, buffer_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, params.buffer.width, params.buffer.height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, params.buffer.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create vao for generating fullscreen triangle
    glGenVertexArrays(1, &params.fullscreen_triangle_vao);

    // Create shader for displaying buffer
    static const char* fragment_shader =
        "\n"
        "#version 330\n"
        "\n"
        "uniform sampler2D buffer;\n"
        "noperspective in vec2 TexCoord;\n"
        "\n"
        "out vec3 outColor;\n"
        "\n"
        "void main(void){\n"
        "    outColor = texture(buffer, TexCoord).rgb;\n"
        "}\n";

    static const char* vertex_shader =
        "\n"
        "#version 330\n"
        "\n"
        "noperspective out vec2 TexCoord;\n"
        "\n"
        "void main(void){\n"
        "\n"
        "    TexCoord.x = (gl_VertexID == 2)? 2.0: 0.0;\n"
        "    TexCoord.y = (gl_VertexID == 1)? 2.0: 0.0;\n"
        "    \n"
        "    gl_Position = vec4(2.0 * TexCoord - 1.0, 0.0, 1.0);\n"
        "}\n";

    GLuint shader_id = glCreateProgram();

    {
        //Create vertex shader
        GLuint shader_vp = glCreateShader(GL_VERTEX_SHADER);

        glShaderSource(shader_vp, 1, &vertex_shader, 0);
        glCompileShader(shader_vp);
        validate_shader(shader_vp, vertex_shader);
        glAttachShader(shader_id, shader_vp);

        glDeleteShader(shader_vp);
    }

    {
        //Create fragment shader
        GLuint shader_fp = glCreateShader(GL_FRAGMENT_SHADER);

        glShaderSource(shader_fp, 1, &fragment_shader, 0);
        glCompileShader(shader_fp);
        validate_shader(shader_fp, fragment_shader);
        glAttachShader(shader_id, shader_fp);

        glDeleteShader(shader_fp);
    }

    glLinkProgram(shader_id);

    if(!validate_program(shader_id)){
        fprintf(stderr, "Error while validating shader.\n");
        glfwTerminate();
        glDeleteVertexArrays(1, &params.fullscreen_triangle_vao);
        delete[] params.buffer.data;
        return -1;
    }

    glUseProgram(shader_id);

    GLint location = glGetUniformLocation(shader_id, "buffer");
    glUniform1i(location, 0);

    //OpenGL setup
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(params.fullscreen_triangle_vao);

    return 0;
}

void prepare_game(Game& game)
{
    game.width = buffer_width;
    game.height = buffer_height;
    game.num_bullets = 0;
    game.num_aliens = NUM_OF_ALIEN_ROWS * 11;
    game.aliens = new Alien[game.num_aliens];

    game.player.x = buffer_width / 2 - 5;
    game.player.y = 32;
    game.player.life = 3;
}

void destory_all(glParams params, Sprites& sprites, SpriteAnimation* alien_animation, Game game, uint8_t* death_counters)
{
    glfwDestroyWindow(params.window);
    glfwTerminate();

    glDeleteVertexArrays(1, &params.fullscreen_triangle_vao);

    for(size_t i = 0; i < 6; ++i)
    {
        delete[] sprites.alien_sprites[i].data;
    }

    delete[] sprites.alien_death_sprite.data;

    for(size_t i = 0; i < 3; ++i)
    {
        delete[] alien_animation[i].frames;
    }
    delete[] params.buffer.data;
    delete[] game.aliens;
    delete[] death_counters;
}

void process_events(Game& game, Sprites& sprites)
{
    if(fire_pressed && game.num_bullets < GAME_MAX_BULLETS)
    {
        game.bullets[game.num_bullets].x = game.player.x + sprites.player_sprite.width / 2;
        game.bullets[game.num_bullets].y = game.player.y + sprites.player_sprite.height;
        game.bullets[game.num_bullets].dir = 2;
        ++game.num_bullets;
    }
    fire_pressed = false;
}

void simulate_player(Game& game, Sprites& sprites)
{
    int player_move_dir = 2 * move_dir;

    if(player_move_dir != 0)
    {
        if(game.player.x + sprites.player_sprite.width + player_move_dir >= game.width)
        {
            game.player.x = game.width - sprites.player_sprite.width;
        }
        else if((int)game.player.x + player_move_dir <= 0)
        {
            game.player.x = 0;
        }
        else game.player.x += player_move_dir;
    }
}
int main(int argc, char* argv[])
{
    glParams params = {}; 
    Game game = {};
    Sprites sprites = {};
    SpriteAnimation alien_animation[NUM_OF_ALIEN_ROWS - 1] {};
    uint32_t clear_color = rgb_to_uint32(0, 128, 0);

    params.buffer.width  = buffer_width;
    params.buffer.height = buffer_height;
    params.buffer.data   = new uint32_t[params.buffer.width * params.buffer.height];
    
    if (setup_gl(params) != 0 ){
        return -1;
    }
    
    prepare_game(game);
    init_sprites(sprites);
    init_aliens(game, alien_animation, sprites);

    uint8_t* death_counters = new uint8_t[game.num_aliens];
    for(size_t i = 0; i < game.num_aliens; ++i)
    {
        death_counters[i] = 10;
    }
    
    game_running = true;
    while (!glfwWindowShouldClose(params.window) && game_running)
    {
        buffer_clear(&params.buffer, clear_color);
        update_aliens_position(game);
        
        // Draw
        for(size_t ai = 0; ai < game.num_aliens; ++ai)
        {
            if(!death_counters[ai]) continue;

            const Alien& alien = game.aliens[ai];
            if(alien.type == ALIEN_DEAD)
            {
                buffer_draw_sprite(&params.buffer, sprites.alien_death_sprite, alien.x, alien.y, rgb_to_uint32(128, 0, 0));
            }
            else
            {
                const SpriteAnimation& animation = alien_animation[alien.type - 1];
                size_t current_frame = animation.time / animation.frame_duration;
                const Sprite& sprite = *animation.frames[current_frame];
                buffer_draw_sprite(&params.buffer, sprite, alien.x, alien.y, rgb_to_uint32(128, 0, 0));
            }
        }

        for(size_t bi = 0; bi < game.num_bullets; ++bi)
        {
            const Bullet& bullet = game.bullets[bi];
            const Sprite& sprite = sprites.bullet_sprite;
            buffer_draw_sprite(&params.buffer, sprite, bullet.x, bullet.y, rgb_to_uint32(128, 0, 0));
        }

        buffer_draw_sprite(&params.buffer, sprites.player_sprite, game.player.x, game.player.y, rgb_to_uint32(128, 0, 0));

        // Update animations
        for(size_t i = 0; i < 3; ++i)
        {
            ++alien_animation[i].time;
            if(alien_animation[i].time == alien_animation[i].num_frames * alien_animation[i].frame_duration)
            {
                alien_animation[i].time = 0;
            }
        }

        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0,
            params.buffer.width, params.buffer.height,
            GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
            params.buffer.data
        );
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glfwSwapBuffers(params.window);

        // Simulate aliens
        for(size_t ai = 0; ai < game.num_aliens; ++ai)
        {
            const Alien& alien = game.aliens[ai];
            if(alien.type == ALIEN_DEAD && death_counters[ai])
            {
                --death_counters[ai];
            }
        }

        // Simulate bullets
        for(size_t bi = 0; bi < game.num_bullets;)
        {
            game.bullets[bi].y += game.bullets[bi].dir;
            if(game.bullets[bi].y >= game.height || game.bullets[bi].y < sprites.bullet_sprite.height)
            {
                game.bullets[bi] = game.bullets[game.num_bullets - 1];
                --game.num_bullets;
                continue;
            }

            // Check hit
            for(size_t ai = 0; ai < game.num_aliens; ++ai)
            {
                const Alien& alien = game.aliens[ai];
                if(alien.type == ALIEN_DEAD) continue;

                const SpriteAnimation& animation = alien_animation[alien.type - 1];
                size_t current_frame = animation.time / animation.frame_duration;
                const Sprite& alien_sprite = *animation.frames[current_frame];
                bool overlap = sprite_overlap_check(
                    sprites.bullet_sprite, game.bullets[bi].x, game.bullets[bi].y,
                    alien_sprite, alien.x, alien.y
                );
                if(overlap)
                {
                    game.aliens[ai].type = ALIEN_DEAD;
                    // NOTE: Hack to recenter death sprite
                    game.aliens[ai].x -= (sprites.alien_death_sprite.width - alien_sprite.width)/2;
                    game.bullets[bi] = game.bullets[game.num_bullets - 1];
                    --game.num_bullets;
                    continue;
                }
            }

            ++bi;
        }

        simulate_player(game, sprites);
        process_events(game, sprites);
        glfwPollEvents();
    }

    destory_all(params, sprites, alien_animation, game, death_counters);

    return 0;
}
