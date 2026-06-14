//**************************************************************************
// Multi-threaded Doom Fire
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 21/03/2025
//
//

//--------------------------------------------------------------------------
// Includes and definitions
//--------------------------------------------------------------------------
#define GL_width  80
#define GL_height 50
#define GL_FPS 150
#include "GL_tty.h"

//--------------------------------------------------------------------------
// Basic Utilities and Multi-thread Support

#include "util.h"

//--------------------------------------------------------------------------


const char* palette[256] = {
    GL_RGB(  0,  0,   0), GL_RGB(  0,   4,  4), GL_RGB(  0,  16, 20), GL_RGB(  0,  28,  36),
    GL_RGB(  0,  32, 44), GL_RGB(  0,  36, 48), GL_RGB( 60,  24, 32), GL_RGB(100,  16,  16),
    GL_RGB(132,  12, 12), GL_RGB(160,   8,  8), GL_RGB(192,   8,  8), GL_RGB(220,   4,   4),
    GL_RGB(252,   0,  0), GL_RGB(252,   0,  0), GL_RGB(252,  12,  0), GL_RGB(252,  28,   0),
    GL_RGB(252,  40,  0), GL_RGB(252,  52,  0), GL_RGB(252,  64,  0), GL_RGB(252,  80,   0),
    GL_RGB(252,  92,  0), GL_RGB(252, 104,  0), GL_RGB(252, 116,  0), GL_RGB(252, 132,   0),
    GL_RGB(252, 144,  0), GL_RGB(252, 156,  0), GL_RGB(252, 156,  0), GL_RGB(252, 160,   0),
    GL_RGB(252, 160,  0), GL_RGB(252, 164,  0), GL_RGB(252, 168,  0), GL_RGB(252, 168,   0),
    GL_RGB(252, 172,  0), GL_RGB(252, 176,  0), GL_RGB(252, 176,  0), GL_RGB(252, 180,   0),
    GL_RGB(252, 180,  0), GL_RGB(252, 184,  0), GL_RGB(252, 188,  0), GL_RGB(252, 188,   0),
    GL_RGB(252, 192,  0), GL_RGB(252, 196,  0), GL_RGB(252, 196,  0), GL_RGB(252, 200,   0),
    GL_RGB(252, 204,  0), GL_RGB(252, 204,  0), GL_RGB(252, 208,  0), GL_RGB(252, 212,   0),
    GL_RGB(252, 212,  0), GL_RGB(252, 216,  0), GL_RGB(252, 220,  0), GL_RGB(252, 220,   0),
    GL_RGB(252, 224,  0), GL_RGB(252, 228,  0), GL_RGB(252, 228,  0), GL_RGB(252, 232,   0),
    GL_RGB(252, 232,  0), GL_RGB(252, 236,  0), GL_RGB(252, 240,  0), GL_RGB(252, 240,   0),
    GL_RGB(252, 244,  0), GL_RGB(252, 248,  0), GL_RGB(252, 248,  0), GL_RGB(252, 252,   0),
#define W GL_RGB(252,252,252)
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
    W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W
#undef W
};

static uint8_t fire[GL_width * GL_height];

// this is a thread safe function that use to add motion blur to each row and col
// of the fire buffer
void line_blur(int offset, int step, int nsteps) {
    uint8_t circ[3] = {0, fire[offset], fire[offset+step]};
    uint8_t beg = 1;
    for (int i=0; i<nsteps; i++) {
        fire[offset] = (circ[0]+circ[1]+circ[2])/3;
        circ[(beg+++2)%3] = i+2<nsteps ? fire[offset+2*step] : 0;
        offset += step;
    }
}




// // Radom number generator with per-core seeds
static long int randomseed[2] = {0, 1};
int myrand(int cid)
{
    randomseed[cid] = (randomseed[cid] * 1366l + 150889l) % 714025l;
    return (int)randomseed[cid];
}

//--------------------------------------------------------------------------
// Main
//
// all threads start executing thread_entry(). Use their "coreid" to
// differentiate between threads (each thread is running on a separate core).
// GL functions are not thread safe therefore they are shared among them

void thread_entry(int cid, int nc)
{

    // enable graphis on serial terminal
    if (cid == 0)
    {
        GL_init();
    }

    // Synchronize to ensure both threads start the loop together
    barrier(nc);

    while (1)
    {

        // 1. Horizontal blur: Split rows between threads
        int rows_per_thread = GL_height / nc;
        int start_row = cid * rows_per_thread;
        int end_row = (cid == nc - 1) ? GL_height : start_row + rows_per_thread; // works oon 2 core enviroment

        for (int j = start_row; j < end_row; j++)
            line_blur(j * GL_width, 1, GL_width);

        barrier(nc); // Ensure horizontal blur completes

        // 2. Vertical blur: Split cols between threads
        int cols_per_thread = GL_width / nc;
        int start_col = cid * cols_per_thread;
        int end_col = (cid == nc - 1) ? GL_width : start_col + cols_per_thread; // works oon 2 core enviroment

        for (int i = start_col; i < end_col; i++)
            line_blur(i, GL_width, GL_height);

        barrier(nc); // Ensure vertical blur completes

        // 3. Cool fire: Split pixels between threads
        int pixels_per_thread = (GL_width * GL_height) / nc;
        int start_pixel = cid * pixels_per_thread;
        int end_pixel = (cid == nc - 1) ? GL_width * GL_height : start_pixel + pixels_per_thread;
        for (int i = start_pixel; i < end_pixel; i++) // cool
            if (!(myrand(cid) & 15) && fire[i] > 0)
                fire[i]--;

        // This function adds fire to the bed and not thread safe therefore should run
        // only one one core in this case core 0
        // 4. Add heat: Only core 0 updates the bottom row
        if (cid == 0)
        {
            for (int i = 0; i < GL_width; i++)
            { // add heat to the bed
                int idx = i + (GL_height - 1) * GL_width;
                if (!(myrand(cid) % 32))
                    fire[idx] = 128 + myrand(cid) % 128; // sparks
                else
                    fire[idx] = fire[idx] < 16 ? 16 : fire[idx]; // ember bed
            }
        }

        barrier(nc); // Ensure cooling and heating complete

        // 5. Display: Core 0 handles all display operations
        if (cid == 0)
        {
            GL_home();
            for (int j = 0; j < GL_height; j += 2)
            { // show the buffer
                for (int i = 0; i < GL_width; i++)
                    GL_set2pixelsIhere(palette, fire[i + j * GL_width], fire[i + (j + 1) * GL_width]);
                GL_newline();
            }
        }

        // 6. Scroll: Split rows between threads
        int scroll_rows_per_thread = (GL_height - 1) / nc;
        int start_j = cid * scroll_rows_per_thread + 1;
        int end_j = (cid == nc - 1) ? GL_height : start_j + scroll_rows_per_thread;
        for (int j = start_j; j < end_j; j++) // scroll up
            for (int i = 0; i < GL_width; i++)
                fire[i + (j - 1) * GL_width] = fire[i + j * GL_width];

        barrier(nc); // Ensure scrolling completes

    }
}

// extern void printf(const char *format, ...);

// void thread_entry(int cid, int nc)
// {

// // Sample variables for testing
//     int y = 5, x = 10;                    // Cursor position
//     int R = 255, G = 0, B = 0;            // RGB for background color
//     int r1 = 0, g1 = 255, b1 = 0;         // RGB for another background color
//     int r2 = 0, g2 = 0, b2 = 255;         // RGB for foreground color
//     const char* cmap[] = {"255;0;0", "0;255;0", "0;0;255"}; // Color map array
//     int c = 0, c1 = 1, c2 = 2;            // Indices for cmap

//     // Test cases
//     printf("\033[%d;%dH", y, x);          // Move cursor to (5, 10)
//     printf("123");
//     printf("\033[48;2;%d;%d;%dm ", R, G, B); // Set background to red, print space
//     printf("22");
//     printf("\033[48;2;%d;%d;%dm", r1, g1, b1); // Set background to green
//     printf("33");
//     printf("\033[38;2;%d;%d;%dm", r2, g2, b2); // Set foreground to blue
//     printf("44");

//     printf("\xE2\x96\x83");               // Print Unicode block character (▃)
//     printf("55");

//     printf("\033[38;2;0;0;0m");           // Set foreground to black
//     printf("66");

//     printf("\033[48;2;0;0;0m\n");         // Set background to black, newline
//     printf("77");

//     printf("\033[48;5;16m" "\033[38;5;15m"); // Set 256-color mode (bg:16, fg:15)
//     printf("88");

//     printf("\033[2J");                    // Clear screen
//     printf("99");

//     printf("\033[H");                     // Move cursor to home (top-left)
//     printf("234");

//     printf("\033[?25l");                  // Hide cursor
//     printf("567");

//     printf("\033[?25h");                  // Show cursor
//     printf("789");

//     printf("\033[48;2;%sm ", cmap[c]);    // Set background from cmap[0], print space
//     printf("hell");

//     printf("\033[48;2;%sm", cmap[c1]);    // Set background from cmap[1]
//     printf("kkk");

//     printf("\033[38;2;%sm", cmap[c2]);    // Set foreground from cmap[2]
//     printf("ccc");


//     while (1)
//         ;
// }

// int myrand() {
//    static long int randomseed = 0;
//    randomseed = (randomseed * 1366l + 150889l) % 714025l;
//    return (int)randomseed;
// }



// void thread_entry(int cid, int nc){
//         GL_init();
//     for (;;) {
//         GL_home();

//         // box blur: first horizontal motion blur then vertical motion blur
//         for (int j = 0; j<GL_height; j++)
//             line_blur(j*GL_width, 1, GL_width);
//         for (int i = 0; i<GL_width; i++)
//             line_blur(i, GL_width, GL_height);
       
//         for (int i = 0; i< GL_width*GL_height; i++) // cool
//             if (!(myrand()&15) && fire[i]>0)
//                 fire[i]--;

//         for (int i = 0; i<GL_width; i++) {       // add heat to the bed
//             int idx = i+(GL_height-1)*GL_width;
//             if (!(myrand()%32))
//                 fire[idx] = 128+myrand()%128;   // sparks
//             else
//                 fire[idx] = fire[idx]<16 ? 16 : fire[idx]; // ember bed
//         }

//         for (int j = 0; j<GL_height; j+=2) {      // show the buffer
//             for (int i = 0; i<GL_width; i++)
// 	        GL_set2pixelsIhere(
// 		   palette,fire[i+j*GL_width],
// 		           fire[i+(j+1)*GL_width]				   
// 		);
// 	    GL_newline();
//         }

//         for (int j = 1; j<GL_height; j++)        // scroll up
//             for (int i = 0; i<GL_width; i++)
//                 fire[i+(j-1)*GL_width] = fire[i+j*GL_width] ;
       

//     }
// }