#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BOARD_HEIGHT      22      
#define LEFT_LANES         2      
#define RIGHT_LANES        2      
#define TOTAL_LANES        (LEFT_LANES + RIGHT_LANES)
#define LANE_WIDTH         8      
#define CENTER_GAP         2      

#define BOARD_PIXEL_W      (TOTAL_LANES * LANE_WIDTH + CENTER_GAP + 2)

#define CAR_ROW            (BOARD_HEIGHT - 2)

#define TICK_US            80000   
#define SPAWN_INTERVAL      9      
#define SPEED_UP_EVERY     20      
#define MIN_SPAWN_RATE      3      

#define OBJ_NONE           0
#define OBJ_COIN           1
#define OBJ_OBSTACLE       2
#define OBJ_INVINCIBLE     3
#define OBJ_MAGNET         4

#define INVINC_DURATION   110
#define MAGNET_DURATION   130
#define MAGNET_RANGE        6

#define MAX_OBJECTS       128

#define CP_DEFAULT         1
#define CP_CAR1            2
#define CP_CAR2            3
#define CP_COIN            4
#define CP_OBS             5
#define CP_INV             6
#define CP_MAG             7
#define CP_BORDER          8
#define CP_TITLE           9
#define CP_HUD             10
#define CP_GAMEOVER        11
#define CP_HIGHLIGHT       12
#define CP_ROAD            13

#define HISCORE_FILE       "/tmp/.twocars_hiscore"

typedef struct {
    int row;
    int lane;    
    int type;
    int active;
} Object;

typedef struct {
    int lane;         
    int group;        
    int score;
    int inv_ticks;    
    int mag_ticks;    
    int alive;
    const char *name;
} Car;

typedef struct {
    Object objects[MAX_OBJECTS];
    Car    cars[2];
    int    tick;
    int    total_score;
    int    spawn_rate;
    int    running;
    int    paused;
    int    level;
} GameState;

static int g_hiscore = 0;

void load_hiscore(void) {
    FILE *f = fopen(HISCORE_FILE, "r");
    if (!f) return;
    fscanf(f, "%d", &g_hiscore);
    fclose(f);
}

void save_hiscore(int score) {
    if (score <= g_hiscore) return;
    g_hiscore = score;
    FILE *f = fopen(HISCORE_FILE, "w");
    if (!f) return;
    fprintf(f, "%d\n", score);
    fclose(f);
}

void init_colors(void) {
    start_color();
    use_default_colors();

    init_pair(CP_DEFAULT,   COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_CAR1,      COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_CAR2,      COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_COIN,      COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_OBS,       COLOR_RED,     COLOR_BLACK);
    init_pair(CP_INV,       COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_MAG,       COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_BORDER,    COLOR_BLUE,    COLOR_BLACK);
    init_pair(CP_TITLE,     COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_HUD,       COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_GAMEOVER,  COLOR_RED,     COLOR_BLACK);
    init_pair(CP_HIGHLIGHT, COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_ROAD,      COLOR_WHITE,   COLOR_BLACK);
}

int lane_to_x(int lane) {
    int x;
    if (lane < LEFT_LANES) {
        x = 1 + lane * LANE_WIDTH + LANE_WIDTH / 2;
    } else {
        int rl = lane - LEFT_LANES;
        x = 1 + LEFT_LANES * LANE_WIDTH + CENTER_GAP + rl * LANE_WIDTH + LANE_WIDTH / 2;
    }
    return x;
}

int car_abs_lane(const Car *car) {
    return car->group * LEFT_LANES + car->lane;
}

void clear_objects(GameState *gs) {
    memset(gs->objects, 0, sizeof(gs->objects));
}

Object *alloc_object(GameState *gs) {
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (!gs->objects[i].active) {
            memset(&gs->objects[i], 0, sizeof(Object));
            gs->objects[i].active = 1;
            return &gs->objects[i];
        }
    }
    return NULL; 
}

void release_object(Object *o) {
    if (o) o->active = 0;
}

void spawn_for_group(GameState *gs, int group) {
    Object *o = alloc_object(gs);
    if (!o) return;

    o->row  = 0;
    o->lane = group * LEFT_LANES + (rand() % LEFT_LANES);

    int r = rand() % 100;
    if      (r <  5) o->type = OBJ_INVINCIBLE;   
    else if (r < 12) o->type = OBJ_MAGNET;        
    else if (r < 45) o->type = OBJ_OBSTACLE;      
    else            o->type = OBJ_COIN;           
}

void maybe_spawn(GameState *gs) {
    if (gs->tick % gs->spawn_rate != 0) return;
    int empty_chance = 40; 

    if ((rand() % 100) > empty_chance) {
        spawn_for_group(gs, 0); 
    }

    if ((rand() % 100) > empty_chance) {
        spawn_for_group(gs, 1); 
    }
}

void apply_magnet(GameState *gs) {
    for (int c = 0; c < 2; c++) {
        if (gs->cars[c].mag_ticks <= 0) continue;

        int abs_lane = car_abs_lane(&gs->cars[c]);

        for (int i = 0; i < MAX_OBJECTS; i++) {
            Object *o = &gs->objects[i];
            if (!o->active || o->type != OBJ_COIN) continue;

            int coin_group = (o->lane < LEFT_LANES) ? 0 : 1;
            if (coin_group != c) continue;

            int dist = CAR_ROW - o->row;
            if (dist > MAGNET_RANGE) continue;

            o->lane = abs_lane;
        }
    }
}

void decay_powerups(GameState *gs) {
    for (int c = 0; c < 2; c++) {
        if (gs->cars[c].inv_ticks > 0) gs->cars[c].inv_ticks--;
        if (gs->cars[c].mag_ticks > 0) gs->cars[c].mag_ticks--;
    }
}

int check_collisions(GameState *gs) {
    for (int i = 0; i < MAX_OBJECTS; i++) {
        Object *o = &gs->objects[i];
        if (!o->active) continue;
        if (o->row != CAR_ROW) continue;

        for (int c = 0; c < 2; c++) {
            if (o->lane != car_abs_lane(&gs->cars[c])) continue;

            switch (o->type) {
                case OBJ_COIN:
                    gs->cars[c].score++;
                    gs->total_score++;
                    break;

                case OBJ_OBSTACLE:
                    if (gs->cars[c].inv_ticks > 0) {
                    } else {
                        gs->cars[c].alive = 0;
                        gs->running = 0;
                        release_object(o);
                        return 1;
                    }
                    break;

                case OBJ_INVINCIBLE:
                    gs->cars[c].inv_ticks = INVINC_DURATION;
                    break;

                case OBJ_MAGNET:
                    gs->cars[c].mag_ticks = MAGNET_DURATION;
                    break;
            }
            release_object(o);
        }
    }
    return 0;
}

void update(GameState *gs) {
    apply_magnet(gs);

    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (!gs->objects[i].active) continue;
        gs->objects[i].row++;
        if (gs->objects[i].row > BOARD_HEIGHT) {
            release_object(&gs->objects[i]);
        }
    }

    if (check_collisions(gs)) return;

    decay_powerups(gs);

    gs->level      = gs->total_score / SPEED_UP_EVERY;
    gs->spawn_rate = SPAWN_INTERVAL - gs->level;
    if (gs->spawn_rate < MIN_SPAWN_RATE) gs->spawn_rate = MIN_SPAWN_RATE;
}

void draw_road(int board_top) {
    attron(COLOR_PAIR(CP_BORDER));

    int left_border  = 0;
    int right_border = 1 + TOTAL_LANES * LANE_WIDTH + CENTER_GAP;
    int center_left  = 1 + LEFT_LANES * LANE_WIDTH;

    for (int y = board_top; y <= board_top + BOARD_HEIGHT; y++) {
        mvaddch(y, left_border,  ACS_VLINE);
        mvaddch(y, right_border, ACS_VLINE);

        mvaddch(y, center_left,     '|');
        mvaddch(y, center_left + 1, '|');
    }

    attroff(COLOR_PAIR(CP_BORDER));
    attron(COLOR_PAIR(CP_ROAD) | A_DIM);
    for (int y = board_top; y < board_top + BOARD_HEIGHT; y++) {
        if (y % 3 == 0) {
            mvaddch(y, 1 + LANE_WIDTH, ':');
            mvaddch(y, 1 + LEFT_LANES * LANE_WIDTH + CENTER_GAP + LANE_WIDTH, ':');
        }
    }
    attroff(COLOR_PAIR(CP_ROAD) | A_DIM);
}

void draw_objects(GameState *gs, int board_top) {
    for (int i = 0; i < MAX_OBJECTS; i++) {
        Object *o = &gs->objects[i];
        if (!o->active) continue;

        int x    = lane_to_x(o->lane);
        int y    = board_top + o->row;
        int pair = CP_DEFAULT;
        char sym = '?';

        switch (o->type) {
            case OBJ_COIN:       pair = CP_COIN; sym = 'O'; break;
            case OBJ_OBSTACLE:   pair = CP_OBS;  sym = '#'; break;
            case OBJ_INVINCIBLE: pair = CP_INV;  sym = '*'; break;
            case OBJ_MAGNET:     pair = CP_MAG;  sym = 'M'; break;
        }

        attron(COLOR_PAIR(pair) | A_BOLD);
        mvaddch(y, x, sym);
        attroff(COLOR_PAIR(pair) | A_BOLD);
    }
}

void draw_car(GameState *gs, int c, int board_top) {
    Car *car = &gs->cars[c];
    int x = lane_to_x(car_abs_lane(car));
    int y = board_top + CAR_ROW;

    int pair = (c == 0) ? CP_CAR1 : CP_CAR2;
    if (car->inv_ticks > 0 && (gs->tick % 4 < 2)) pair = CP_INV;
    else if (car->mag_ticks > 0 && (gs->tick % 6 < 3)) pair = CP_MAG;

    attron(COLOR_PAIR(pair) | A_BOLD);
    mvaddstr(y, x - 1, "/A\\");
    attroff(COLOR_PAIR(pair) | A_BOLD);

    if (car->inv_ticks > 0) {
        attron(COLOR_PAIR(CP_INV));
        mvaddch(y - 1, x - 2, '*');
        mvaddch(y - 1, x,     '*');
        mvaddch(y - 1, x + 2, '*');
        attroff(COLOR_PAIR(CP_INV));
    }

    if (car->mag_ticks > 0) {
        attron(COLOR_PAIR(CP_MAG) | A_DIM);
        mvaddch(y - 2, x - 2, '~');
        mvaddch(y - 2, x,     '~');
        mvaddch(y - 2, x + 2, '~');
        attroff(COLOR_PAIR(CP_MAG) | A_DIM);
    }
}

void draw_hud(GameState *gs, int hud_top) {
    int x = 2;

    attron(COLOR_PAIR(CP_HUD) | A_BOLD);
    mvprintw(hud_top, x,
             "Score: %d  |  Hi: %d  |  Level: %d",
             gs->total_score,
             (gs->total_score > g_hiscore ? gs->total_score : g_hiscore),
             gs->level + 1);
    attroff(COLOR_PAIR(CP_HUD) | A_BOLD);

    attron(COLOR_PAIR(CP_CAR1) | A_BOLD);
    mvaddstr(hud_top + 1, x, "LEFT  ");
    attroff(COLOR_PAIR(CP_CAR1) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    printw("Score:%3d  ", gs->cars[0].score);
    attroff(COLOR_PAIR(CP_HUD));

    if (gs->cars[0].inv_ticks > 0) {
        attron(COLOR_PAIR(CP_INV) | A_BOLD);
        printw("[INV:%3d] ", gs->cars[0].inv_ticks);
        attroff(COLOR_PAIR(CP_INV) | A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_HUD) | A_DIM);
        printw("[---    ] ");
        attroff(COLOR_PAIR(CP_HUD) | A_DIM);
    }
    if (gs->cars[0].mag_ticks > 0) {
        attron(COLOR_PAIR(CP_MAG) | A_BOLD);
        printw("[MAG:%3d]", gs->cars[0].mag_ticks);
        attroff(COLOR_PAIR(CP_MAG) | A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_HUD) | A_DIM);
        printw("[---    ]");
        attroff(COLOR_PAIR(CP_HUD) | A_DIM);
    }

    attron(COLOR_PAIR(CP_CAR2) | A_BOLD);
    mvaddstr(hud_top + 2, x, "RIGHT ");
    attroff(COLOR_PAIR(CP_CAR2) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    printw("Score:%3d  ", gs->cars[1].score);
    attroff(COLOR_PAIR(CP_HUD));

    if (gs->cars[1].inv_ticks > 0) {
        attron(COLOR_PAIR(CP_INV) | A_BOLD);
        printw("[INV:%3d] ", gs->cars[1].inv_ticks);
        attroff(COLOR_PAIR(CP_INV) | A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_HUD) | A_DIM);
        printw("[---    ] ");
        attroff(COLOR_PAIR(CP_HUD) | A_DIM);
    }
    if (gs->cars[1].mag_ticks > 0) {
        attron(COLOR_PAIR(CP_MAG) | A_BOLD);
        printw("[MAG:%3d]", gs->cars[1].mag_ticks);
        attroff(COLOR_PAIR(CP_MAG) | A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_HUD) | A_DIM);
        printw("[---    ]");
        attroff(COLOR_PAIR(CP_HUD) | A_DIM);
    }

    attron(COLOR_PAIR(CP_HUD) | A_DIM);
    mvprintw(hud_top + 3, x,
             "A/D = Left car   J/L = Right car   P = Pause   Q = Quit");
    attroff(COLOR_PAIR(CP_HUD) | A_DIM);

    attron(COLOR_PAIR(CP_COIN)  | A_BOLD); mvaddstr(hud_top + 4, x, "O"); attroff(COLOR_PAIR(CP_COIN) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));            printw("=Coin  ");
    attron(COLOR_PAIR(CP_OBS)   | A_BOLD); printw("#"); attroff(COLOR_PAIR(CP_OBS) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));            printw("=Obstacle  ");
    attron(COLOR_PAIR(CP_INV)   | A_BOLD); printw("*"); attroff(COLOR_PAIR(CP_INV) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));            printw("=Invincible  ");
    attron(COLOR_PAIR(CP_MAG)   | A_BOLD); printw("M"); attroff(COLOR_PAIR(CP_MAG) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));            printw("=Magnet");
    attroff(COLOR_PAIR(CP_HUD));
}

void draw(GameState *gs, int board_top) {
    erase();
    draw_road(board_top);
    draw_objects(gs, board_top);
    draw_car(gs, 0, board_top);
    draw_car(gs, 1, board_top);
    draw_hud(gs, board_top + BOARD_HEIGHT + 2);
    refresh();
}

void draw_paused(int board_top) {
    int y = board_top + BOARD_HEIGHT / 2 - 1;
    int x = BOARD_PIXEL_W / 2 - 6;

    attron(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
    mvprintw(y,     x, "                ");
    mvprintw(y + 1, x, "    ─ PAUSED ─   ");
    mvprintw(y + 2, x, "  P to continue  ");
    mvprintw(y + 3, x, "                ");
    attroff(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
    refresh();
}

void draw_title(void) {
    clear();

    int starty = 2;
    int startx = 4;

    attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvaddstr(starty + 0, startx, "  |||||||| ||     ||   ||||||       ||||||  |||||  ||||||  ||||||| ");
    mvaddstr(starty + 1, startx, "     ||    ||     ||  ||    ||     ||      ||   || ||   || ||      ");
    mvaddstr(starty + 2, startx, "     ||    ||  |  ||  ||    ||     ||      ||||||| ||||||  ||||||| ");
    mvaddstr(starty + 3, startx, "     ||    || ||| ||  ||    ||     ||      ||   || ||   ||      || ");
    mvaddstr(starty + 4, startx, "     ||     ||   ||    ||||||       |||||| ||   || ||   || ||||||| ");
    attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);

    int y = starty + 7;

    attron(COLOR_PAIR(CP_BORDER));
    mvhline(y++, 2, ACS_HLINE, 68);
    attroff(COLOR_PAIR(CP_BORDER));
    y++;

    attron(COLOR_PAIR(CP_HUD) | A_BOLD);
    mvaddstr(y++, 6, "CONTROLS");
    attroff(COLOR_PAIR(CP_HUD) | A_BOLD);

    attron(COLOR_PAIR(CP_CAR1) | A_BOLD);
    mvaddstr(y, 6, "  Left  Car");
    attroff(COLOR_PAIR(CP_CAR1) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 18, " :  A = move left        D = move right");
    attroff(COLOR_PAIR(CP_HUD));

    attron(COLOR_PAIR(CP_CAR2) | A_BOLD);
    mvaddstr(y, 6, "  Right Car");
    attroff(COLOR_PAIR(CP_CAR2) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 18, " :  J = move left        L = move right");
    attroff(COLOR_PAIR(CP_HUD));

    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 6, "  Pause      :  P                        Q = Quit");
    attroff(COLOR_PAIR(CP_HUD));
    y++;

    attron(COLOR_PAIR(CP_HUD) | A_BOLD);
    mvaddstr(y++, 6, "OBJECTS");
    attroff(COLOR_PAIR(CP_HUD) | A_BOLD);

    attron(COLOR_PAIR(CP_COIN) | A_BOLD);
    mvaddch(y, 8, 'O');
    attroff(COLOR_PAIR(CP_COIN) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 10, "  Coin            collect for +1 point");
    attroff(COLOR_PAIR(CP_HUD));

    attron(COLOR_PAIR(CP_OBS) | A_BOLD);
    mvaddch(y, 8, '#');
    attroff(COLOR_PAIR(CP_OBS) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 10, "  Obstacle        instant game over (unless invincible)");
    attroff(COLOR_PAIR(CP_HUD));

    attron(COLOR_PAIR(CP_INV) | A_BOLD);
    mvaddch(y, 8, '*');
    attroff(COLOR_PAIR(CP_INV) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 10, "  Invincible      smash through obstacles temporarily");
    attroff(COLOR_PAIR(CP_HUD));

    attron(COLOR_PAIR(CP_MAG) | A_BOLD);
    mvaddch(y, 8, 'M');
    attroff(COLOR_PAIR(CP_MAG) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvaddstr(y++, 10, "  Magnet          auto-attracts nearby coins to your lane");
    attroff(COLOR_PAIR(CP_HUD));
    y++;

    attron(COLOR_PAIR(CP_HUD) | A_BOLD);
    mvaddstr(y++, 6, "TIPS");
    attroff(COLOR_PAIR(CP_HUD) | A_BOLD);
    attron(COLOR_PAIR(CP_HUD));
    mvprintw(y++, 6, "  Game speeds up every %d points   survive as long as you can!", SPEED_UP_EVERY);
    mvaddstr(y++, 6, "  Each car controls independently   both must stay alive.");
    attroff(COLOR_PAIR(CP_HUD));
    y++;

    attron(COLOR_PAIR(CP_BORDER));
    mvhline(y++, 2, ACS_HLINE, 68);
    attroff(COLOR_PAIR(CP_BORDER));

    if (g_hiscore > 0) {
        attron(COLOR_PAIR(CP_COIN) | A_BOLD);
        mvprintw(y++, 6, "  Best Score So Far: %d", g_hiscore);
        attroff(COLOR_PAIR(CP_COIN) | A_BOLD);
    }

    attron(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD | A_BLINK);
    mvaddstr(y + 1, 6, "     Press SPACE to start   or   Q to quit     ");
    attroff(COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD | A_BLINK);

    refresh();
}

void draw_game_over(GameState *gs) {
    int new_hi = (gs->total_score > g_hiscore);
    save_hiscore(gs->total_score);
    clear();

    int y = 6;
    int x = 10;
    attron(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);
    mvaddstr(y++, x, "========================================");
    mvaddstr(y++, x, "              GAME  OVER                ");
    mvaddstr(y++, x, "========================================");
    attroff(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);

    const char *loser = (!gs->cars[0].alive) ? "Left Car" : "Right Car";
    attron(COLOR_PAIR(CP_OBS) | A_BOLD);
    mvprintw(y++, x, "   %-36s  ", "");
    mvprintw(y - 1, x + 4, "%s crashed!", loser);
    attroff(COLOR_PAIR(CP_OBS) | A_BOLD);

    attron(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);
    mvaddstr(y++, x, "========================================");
    attroff(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);

    attron(COLOR_PAIR(CP_CAR1) | A_BOLD);
    mvprintw(y++, x, "   Left  Car  Score : %3d                ", gs->cars[0].score);
    attroff(COLOR_PAIR(CP_CAR1) | A_BOLD);

    attron(COLOR_PAIR(CP_CAR2) | A_BOLD);
    mvprintw(y++, x, "   Right Car  Score : %3d                ", gs->cars[1].score);
    attroff(COLOR_PAIR(CP_CAR2) | A_BOLD);

    attron(COLOR_PAIR(CP_COIN) | A_BOLD);
    mvprintw(y++, x, "   Total Score      : %3d                ", gs->total_score);
    attroff(COLOR_PAIR(CP_COIN) | A_BOLD);

    attron(COLOR_PAIR(CP_HUD));
    mvprintw(y++, x, "   Level Reached    : %3d                ", gs->level + 1);
    attroff(COLOR_PAIR(CP_HUD));

    if (new_hi) {
        attron(COLOR_PAIR(CP_INV) | A_BOLD | A_BLINK);
        mvprintw(y++, x, "        ★  NEW HIGH SCORE!  ★           ");
        attroff(COLOR_PAIR(CP_INV) | A_BOLD | A_BLINK);
    } else {
        attron(COLOR_PAIR(CP_HUD) | A_DIM);
        mvprintw(y++, x, "   Best Score       : %3d                ", g_hiscore);
        attroff(COLOR_PAIR(CP_HUD) | A_DIM);
    }

    attron(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);
    mvaddstr(y++, x, "========================================");
    attroff(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);

    attron(COLOR_PAIR(CP_HUD));
    mvprintw(y++, x, "   Press  R  to restart                 ");
    mvprintw(y++, x, "   Press  Q  to quit                    ");
    attroff(COLOR_PAIR(CP_HUD));

    attron(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);
    mvaddstr(y++, x, "========================================");
    attroff(COLOR_PAIR(CP_GAMEOVER) | A_BOLD);

    refresh();
}

void init_game(GameState *gs) {
    memset(gs, 0, sizeof(*gs));

    gs->cars[0].group = 0;
    gs->cars[0].lane  = 0;
    gs->cars[0].alive = 1;
    gs->cars[0].name  = "Left Car";

    gs->cars[1].group = 1;
    gs->cars[1].lane  = 1;
    gs->cars[1].alive = 1;
    gs->cars[1].name  = "Right Car";

    gs->running    = 1;
    gs->spawn_rate = SPAWN_INTERVAL;
    gs->level      = 0;
}

int handle_input_title(void) {
    while (1) {
        int ch = getch();
        if (ch == ' ')            return 0;   
        if (ch == 'q' || ch == 'Q') return 1; 
        usleep(20000);
    }
}

int handle_input_gameover(void) {
    while (1) {
        int ch = getch();
        if (ch == 'r' || ch == 'R') return 1; 
        if (ch == 'q' || ch == 'Q') return 0; 
        usleep(20000);
    }
}

int handle_input_game(GameState *gs, int board_top) {
    int ch = getch();
    if (ch == ERR) return 0; 

    if (ch == 'q' || ch == 'Q') { gs->running = 0; return 1; }

    if (ch == 'p' || ch == 'P') {
        gs->paused = !gs->paused;
        if (gs->paused) {
            draw_paused(board_top);
            nodelay(stdscr, FALSE);
            while (1) {
                int c2 = getch();
                if (c2 == 'p' || c2 == 'P') break;
                if (c2 == 'q' || c2 == 'Q') { gs->running = 0; gs->paused = 0; break; }
            }
            nodelay(stdscr, TRUE);
            gs->paused = 0;
        }
        return 0;
    }

    if (ch == 'a' || ch == 'A') gs->cars[0].lane = 0;
    if (ch == 'd' || ch == 'D') gs->cars[0].lane = 1;

    if (ch == 'j' || ch == 'J') gs->cars[1].lane = 0;
    if (ch == 'l' || ch == 'L') gs->cars[1].lane = 1;

    return 0;
}

int game_loop(int board_top) {
    GameState gs;
    init_game(&gs);

    while (gs.running) {
        int quit = handle_input_game(&gs, board_top);
        if (quit) return 0;

        maybe_spawn(&gs);
        update(&gs);
        draw(&gs, board_top);

        gs.tick++;
        usleep(TICK_US);
    }

    draw_game_over(&gs);
    return handle_input_gameover();
}

int main(void) {
    srand((unsigned)time(NULL));
    load_hiscore();

    initscr();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    init_colors();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int board_top = (rows > BOARD_HEIGHT + 12) ? (rows - BOARD_HEIGHT - 10) / 2 : 0;

    draw_title();
    nodelay(stdscr, FALSE); 
    int rc = handle_input_title();
    nodelay(stdscr, TRUE);
    if (rc == 1) { endwin(); return 0; }

    int result = 1; 
    while (result == 1) {
        result = game_loop(board_top);
    }

    clear();
    refresh();
    endwin();

    printf("\nThanks for playing Two Cars!\n");
    if (g_hiscore > 0)
        printf("Your best score: %d\n\n", g_hiscore);

    return 0;
}
