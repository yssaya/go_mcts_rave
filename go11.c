#define _CRT_SECURE_NO_WARNINGS     // sprintf is Err in VC++

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <limits.h>
#include <ctype.h>


double komi = 6.5;

int B_SIZE = 9;
int WIDTH;
#define MAX_B_SIZE 19
#define BOARD_MAX ((MAX_B_SIZE+2)*(MAX_B_SIZE+2))

int board[BOARD_MAX];
int dir4[4];
int ko_z;

#define MAX_MOVES 1000
int record[MAX_MOVES];
int moves = 0;

int all_playouts = 0;
int flag_record_playout = 0;
int left_time[2];

#define D_MAX 1000
int path[D_MAX];
int depth;
int area[BOARD_MAX];
double winrate;

int get_z(int x,int y)
{
  return y*WIDTH + x;  // 1<= x <=9, 1<= y <=9
}

int get81(int z)            // for display only
{
  int y = z / WIDTH;
  int x = z - y*WIDTH;    // 106 = 9*11 + 7 = (x,y)=(7,9) -> 79
  if ( z==0 ) return 0;
  if ( B_SIZE > 9 ) return x*100 + y;
  return x*10 + y;
}

// don't call twice in same sentence. like prt("z0=%s,z1=%s\n",get_char_z(z0),get_char_z(z1));
char *get_char_z(int z)
{
  int x,y,ax;
  static char buf[16];
  sprintf(buf,"pass");
  if ( z==0 ) return buf;
  y = z / WIDTH;
  x = z - y*WIDTH;
  ax = x-1+'A';
  if ( ax >= 'I' ) ax++;  // from 'A' to 'T', excluding 'I'
  sprintf(buf,"%c%d",ax,B_SIZE+1 - y);
  return buf;
}

int flip_color(int col)
{
  return 3 - col;
}

void prt(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
//{ FILE *fp = fopen("out.txt","a"); if ( fp ) { vfprt( fp, fmt, ap ); fclose(fp); } }
  vfprintf( stderr, fmt, ap );
  va_end(ap);
}
void send_gtp(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vfprintf( stdout, fmt, ap );
  va_end(ap);
}

void print_board()
{
  int x,y;
  const char *str[4] = { ".","X","O","#" };

  prt("   ");
//for (x=0;x<B_SIZE;x++) prt("%d",x+1);
  for (x=0;x<B_SIZE;x++) prt("%c",'A'+x+(x>7));
  prt("\n");
  for (y=0;y<B_SIZE;y++) {
//  prt("%2d ",y+1);
    prt("%2d ",B_SIZE-y);
    for (x=0;x<B_SIZE;x++) {
      prt("%s",str[board[get_z(x+1,y+1)]]);
    }
    if ( y==4 ) prt("  ko_z=%4d,moves=%d",get81(ko_z), moves);
    prt("\n");
  }
}

int check_board[BOARD_MAX];

void count_liberty_sub(int tz, int color, int *p_liberty, int *p_stone)
{
  int z,i;

  check_board[tz] = 1;     // search flag
  (*p_stone)++;            // number of stone
  for (i=0;i<4;i++) {
    z = tz + dir4[i];
    if ( check_board[z] ) continue;
    if ( board[z] == 0 ) {
      check_board[z] = 1;
      (*p_liberty)++;      // number of liberty
    }
    if ( board[z] == color ) count_liberty_sub(z, color, p_liberty, p_stone);
  }
}

void count_liberty(int tz, int *p_liberty, int *p_stone)
{
  int i;
  *p_liberty = *p_stone = 0;
  for (i=0;i<BOARD_MAX;i++) check_board[i] = 0;
  count_liberty_sub(tz, board[tz], p_liberty, p_stone);
}

void take_stone(int tz,int color)
{
  int z,i;
  
  board[tz] = 0;
  for (i=0;i<4;i++) {
    z = tz + dir4[i];
    if ( board[z] == color ) take_stone(z,color);
  }
}

const int FILL_EYE_ERR = 1;
const int FILL_EYE_OK  = 0;

// put stone. success returns 0. in playout, fill_eye_err = 1.
int put_stone(int tz, int color, int fill_eye_err)
{
  int around[4][3];
  int un_col = flip_color(color);
  int space = 0;
  int wall  = 0;
  int mycol_safe = 0;
  int capture_sum = 0;
  int ko_maybe = 0;
  int liberty, stone;
  int i;

  if ( tz == 0 ) { ko_z = 0; return 0; }  // pass

  // count 4 neighbor's liberty and stones.
  for (i=0;i<4;i++) {
    int z, c, liberty, stone;
    around[i][0] = around[i][1] = around[i][2] = 0;
    z = tz+dir4[i];
    c = board[z];  // color
    if ( c == 0 ) space++;
    if ( c == 3 ) wall++;
    if ( c == 0 || c == 3 ) continue;
    count_liberty(z, &liberty, &stone);
    around[i][0] = liberty;
    around[i][1] = stone;
    around[i][2] = c;
    if ( c == un_col && liberty == 1 ) { capture_sum += stone; ko_maybe = z; }
    if ( c == color  && liberty >= 2 ) mycol_safe++;
  }

  if ( capture_sum == 0 && space == 0 && mycol_safe == 0 ) return 1; // suicide
  if ( tz == ko_z                                        ) return 2; // ko
  if ( wall + mycol_safe == 4 && fill_eye_err            ) return 3; // eye
  if ( board[tz] != 0                                    ) return 4;

  for (i=0;i<4;i++) {
    int lib = around[i][0];
    int c   = around[i][2];
    if ( c == un_col && lib == 1 && board[tz+dir4[i]] ) {
      take_stone(tz+dir4[i],un_col);
    }
  }

  board[tz] = color;

  count_liberty(tz, &liberty, &stone);
  if ( capture_sum == 1 && stone == 1 && liberty == 1 ) ko_z = ko_maybe;
  else ko_z = 0;
  return 0;
}

int count_score(int turn_color)
{
  int x,y,i;
  int score = 0, win;
  int black_area = 0, white_area = 0, black_sum, white_sum;
  int mk[4];
  int kind[3];

  kind[0] = kind[1] = kind[2] = 0;
  for (y=0;y<B_SIZE;y++) for (x=0;x<B_SIZE;x++) {
    int z = get_z(x+1,y+1);
    int c = board[z];
    kind[c]++;
    area[z] += (c==1) - (c==2);
    if ( c != 0 ) continue;
    mk[1] = mk[2] = 0;  
    for (i=0;i<4;i++) mk[ board[z+dir4[i]] ]++;
    if ( mk[1] && mk[2]==0 ) { black_area++; area[z]++; }
    if ( mk[2] && mk[1]==0 ) { white_area++; area[z]--; }
  }
 
  black_sum = kind[1] + black_area;
  white_sum = kind[2] + white_area;
  score = black_sum - white_sum;

  win = 0;
  if ( score - komi > 0 ) win = 1;

  if ( turn_color == 2 ) win = -win; 

//prt("black_sum=%2d, (stones=%2d, area=%2d)\n",black_sum, kind[1], black_area);
//prt("white_sum=%2d, (stones=%2d, area=%2d)\n",white_sum, kind[2], white_area);
//prt("score=%d, win=%d\n",score, win);
  return win;
}

int playout(int turn_color)
{
  int color =  turn_color;
  int previous_z = 0;
  int loop;
  int loop_max = B_SIZE*B_SIZE + 200;  // for triple ko

  all_playouts++;
  for (loop=0; loop<loop_max; loop++) {
    // all empty points are candidates.
    int empty[BOARD_MAX][2];  // [0]...z, [1]...probability
    int empty_num = 0;
    int prob_sum = 0;
    int x,y,z,err,pr;
    for (y=0;y<B_SIZE;y++) for (x=0;x<B_SIZE;x++) {
      int z = get_z(x+1,y+1);
      if ( board[z] != 0 ) continue;
      empty[empty_num][0] = z;
      pr = 100;
//    pr = get_prob(z, previous_z, color);
      empty[empty_num][1] = pr;
      prob_sum += pr;
      empty_num++;
    }
    for (;;) {
      int i = 0;
      if ( empty_num == 0 ) {
        z = 0;
      } else {
        int r = rand() % prob_sum;
        int sum = 0;
        for (i=0; i<empty_num; i++) {
          sum += empty[i][1];    // 0,1,2   [0]=1, [1]=1, [2]=1 
          if ( sum > r ) break;
        }
        if ( i==empty_num ) { prt("Err! prob_sum=%d,sum=%d,r=%d,r=%d\n",prob_sum,sum,r,i); exit(0); }
        z = empty[i][0]; 
      }
      err = put_stone(z, color, FILL_EYE_ERR);
      if ( err == 0 ) break;  // pass is ok.
      prob_sum -= empty[i][1];
      empty[i][0] = empty[empty_num-1][0];  // err, delete
      empty[i][1] = empty[empty_num-1][1];
      empty_num--;
    }
    if ( flag_record_playout ) record[moves++] = z;

    if ( depth < D_MAX ) path[depth++] = z;

    if ( z == 0 && previous_z == 0 ) break;  // continuous pass
    previous_z = z;
//  prt("loop=%d,z=%s,c=%d,empty_num=%d,ko_z=%d\n",loop,get_char_z(z),color,empty_num,ko_z);
    color = flip_color(color);
  }
  return count_score(turn_color);
}

int primitive_monte_calro(int color)
{
  int    try_num    = 30; // number of playout
  int    best_z     =  0;
  double best_value;
  double win_rate;
  int x,y,err,i,win_sum,win;

  int ko_z_copy;
  int board_copy[BOARD_MAX];  // keep current board
  ko_z_copy = ko_z;
  memcpy(board_copy, board, sizeof(board));
  memset(area, 0, sizeof(area));

  best_value = -100;

  // try all empty point
  for (y=0;y<B_SIZE;y++) for (x=0;x<B_SIZE;x++) {
    int z = get_z(x+1,y+1);
    if ( board[z] != 0 ) continue;

    err = put_stone(z, color, FILL_EYE_ERR);
    if ( err != 0 ) continue;

    win_sum = 0;
    for (i=0;i<try_num;i++) {
      int board_copy2[BOARD_MAX];
      int ko_z_copy2 = ko_z;
      memcpy(board_copy2, board, sizeof(board));

      win = -playout(flip_color(color));
      win_sum += win;

      ko_z = ko_z_copy2;
      memcpy(board, board_copy2, sizeof(board));
    }
    win_rate = (double)win_sum / try_num;
//  print_board();
//  prt("z=%d,win=%5.3f\n",get81(z),win_rate);
    
    if ( win_rate > best_value ) {
      best_value = win_rate;
      best_z = z;
//    prt("best_z=%d,color=%d,v=%5.3f,try_num=%d\n",get81(best_z),color,best_value,try_num);
    }

    ko_z = ko_z_copy;
    memcpy(board, board_copy, sizeof(board));  // resume board
  }
  return best_z;
}



// following are for UCT

typedef struct {
  int    z;          // move position
  int    games;      // number of games
  double rate;       // winrate
  int    rave_games; // (RAVE) number of games
  double rave_rate;  // (RAVE) winrate
  int    next;       // next node
  double bonus;      // shape bonus
} CHILD;

#define CHILD_SIZE  (MAX_B_SIZE*MAX_B_SIZE+1)  // +1 for PASS

typedef struct {
  int child_num;
  CHILD child[CHILD_SIZE];
  int child_games_sum;
  int child_rave_games_sum;
} NODE;

#define NODE_MAX 10000
NODE node[NODE_MAX];
int node_num = 0;
const int NODE_EMPTY = -1; // no next node
const int ILLEGAL_Z  = -1; // illegal move


void add_child(NODE *pN, int z, double bonus)
{
  int n = pN->child_num;
  pN->child[n].z          = z;
  pN->child[n].games      = 0;
  pN->child[n].rate       = 0;
  pN->child[n].rave_games = 0;
  pN->child[n].rave_rate  = 0;
  pN->child[n].next       = NODE_EMPTY;
  pN->child[n].bonus      = bonus;  // from 0 to 10, good move has big bonus.
  pN->child_num++;
}

// create new node. return node index.
int create_node(int prev_z)
{
  int x,y,z,i,j;
  NODE *pN;
  
  if ( node_num == NODE_MAX ) { prt("node over Err\n"); exit(0); }
  pN = &node[node_num];
  pN->child_num = 0;
  pN->child_games_sum = 0;
  pN->child_rave_games_sum = 0;

  for (y=0;y<B_SIZE;y++) for (x=0;x<B_SIZE;x++) {
    z = get_z(x+1,y+1);
    if ( board[z] != 0 ) continue;
    add_child(pN, z, 0);
  }
  add_child(pN, 0, 0);  // add PASS

  // sort children
  for (i=0; i<pN->child_num-1; i++) {
    double max_b = pN->child[i].bonus;
    int    max_i = i;
    CHILD tmp;
    for (j=i+1; j<pN->child_num; j++) {
      CHILD *c = &pN->child[j];
      if ( max_b >= c->bonus ) continue;
      max_b = c->bonus;
      max_i = j;
    }
    if ( max_i == i ) continue;
    tmp              = pN->child[    i];
    pN->child[    i] = pN->child[max_i];
    pN->child[max_i] = tmp;
  }

  node_num++;
  return node_num-1; 
}

int select_best_ucb(int node_n, int color)
{
  NODE *pN = &node[node_n];
  int select = -1;
  double max_ucb = -999;
  double ucb = 0, ucb_rave = 0, beta;
  int i;

  for (i=0; i<pN->child_num; i++) {
    CHILD *c = &pN->child[i];
    if ( c->z == ILLEGAL_Z ) continue;

    if ( c->games == 0 ) {
      ucb_rave = 10000 + (rand() & 0x7fff);  // try once
    } else {
      const double C = 0.30;    // depends on program
      const double RAVE_D = 3000;
      double moveCount = c->games;
      double raveCount = c->rave_games;
      double rave = c->rave_rate;
      if ( c->z == 0 ) {  // dont select pass easily
        rave = 1 - color;
        raveCount = pN->child_games_sum;
      }

      beta = raveCount / (raveCount + moveCount + raveCount*moveCount/ RAVE_D);

      if ( c->games == 0 ) {  // initial winrate is loss. It is needed when ucb uses move probability like AlphaZero(puct).
        if ( color == 1 ) c->rate = 0;
        if ( color == 2 ) c->rate = -1;
      }
      ucb  = c->rate + C * sqrt( log((double)pN->child_games_sum) / c->games );

      ucb_rave = beta * rave + (1 - beta) * ucb;
//    if ( depth==0 ) prt("%2d:z=%2d,rate=%6.3f,games=%4d, rave_r=%6.3f,g=%4d, beta=%f,ucb_rave=%f\n", i, get81(c->z), c->rate, c->games, c->rave_rate, c->rave_games,beta,ucb_rave);
    }
    if ( ucb_rave > max_ucb ) {
      max_ucb = ucb_rave;
      select = i;
    }
  }
  if ( select == -1 ) { prt("Err! select\n"); exit(0); }
  return select;
}

void update_rave(NODE *pN, int color, int current_depth, double win)
{
  int played_color[BOARD_MAX];
  int i,z;
  int c = color;
  
  memset(played_color, 0, sizeof(played_color));
  for (i=current_depth; i<depth; i++) {
    z = path[i];
    if ( played_color[z] == 0 ) played_color[z] = c;
    c = flip_color(c);
  }

  played_color[0] = 0;	// ignore pass

  for (i=0; i<pN->child_num; i++) {
    CHILD *c = &pN->child[i];
    if ( c->z == ILLEGAL_Z ) continue;
    if ( played_color[c->z] != color ) continue;
    c->rave_rate = (c->rave_games * c->rave_rate + win) / (c->rave_games + 1);
    c->rave_games++;
    pN->child_rave_games_sum++;
  }
}

int search_uct(int color, int node_n)
{
  NODE *pN = &node[node_n];
  CHILD *c = NULL;  
  int select, z, err, win, current_depth;
  for (;;) {
    select = select_best_ucb(node_n, color);
    c = &pN->child[select];
    z = c->z;
    err = put_stone(z, color, FILL_EYE_ERR);
    if ( err == 0 ) break;
    c->z = ILLEGAL_Z;     // select other move
  }

  current_depth = depth;
  path[depth++] = c->z;

  // playout in first time. <= 10 can reduce node.
  if ( c->games <= 0 || depth == D_MAX || (c->z == 0 && depth>=2 && path[depth-2]==0) ) {
    win = -playout(flip_color(color));
  } else {
    if ( c->next == NODE_EMPTY ) c->next = create_node(c->z);
    win = -search_uct(flip_color(color), c->next);
  }

  update_rave(pN, color, current_depth, win);
  
  // update winrate
  c->rate = (c->rate * c->games + win) / (c->games + 1);
  c->games++;
  pN->child_games_sum++;
  return win;  
}

int uct_loop = 1000;  // number of uct loop

int get_best_uct(int color)
{
  int next, i, best_z, best_i = -1;
  double max = -999;
  NODE *pN;
  int prev_z = 0;

  if ( moves > 0 ) prev_z = record[moves-1];
  node_num = 0;
  next = create_node(prev_z);
  memset(area, 0, sizeof(area));

  for (i=0; i<uct_loop; i++) {
    int board_copy[BOARD_MAX];
    int ko_z_copy = ko_z;
    memcpy(board_copy, board, sizeof(board));

    depth = 0;
    search_uct(color, next);

    ko_z = ko_z_copy;
    memcpy(board, board_copy, sizeof(board));
  }
  pN = &node[next];
  for (i=0; i<pN->child_num; i++) {
    CHILD *c = &pN->child[i];
//  double v = c->games;
    double v = c->games + c->rave_rate/2 + (c->z == 0) * -2;	// trick for 100 playout on 19x19
    if ( v > max ) {
      best_i = i;
      max = v;
    }
//    prt("%3d:z=%4d,rate=%6.3f,games=%4d, rave_r=%6.3f,g=%4d\n",
//        i, get81(c->z), c->rate, c->games, c->rave_rate, c->rave_games);
  }
  best_z  = pN->child[best_i].z;
  winrate = pN->child[best_i].rate;
  prt("best_z=%4d,winrate=%6.3f,games=%4d,playouts=%d,nodes=%d\n",
       get81(best_z), winrate, (int)max, all_playouts, node_num);
  return best_z;
}

void change_board_size(int b_size)
{
  B_SIZE = b_size;
  WIDTH = (B_SIZE + 2);
  dir4[0] = +1;
  dir4[1] = +WIDTH;
  dir4[2] = -1;
  dir4[3] = -WIDTH;
}

void init_board(int b_size)
{
  int i,x,y;
  change_board_size(b_size);
  for (i=0;i<BOARD_MAX;i++) board[i] = 3;
  for (y=0;y<B_SIZE;y++) for (x=0;x<B_SIZE;x++) board[get_z(x+1,y+1)] = 0;
  moves = 0;
  ko_z = 0;
}

void add_moves(int z, int color)
{
  int err = put_stone(z, color, FILL_EYE_OK);
  if ( err != 0 ) { prt("Err!\n"); exit(0); }
  record[moves] = z;
  moves++;
  print_board();
}

const int SEARCH_PRIMITIVE = 0;
const int SEARCH_UCT       = 1;

int get_computer_move(int color, int search)
{
  clock_t st = clock();
  double t;
  int z;

  all_playouts = 0;
  if ( search == SEARCH_UCT ) {
    z = get_best_uct(color);
  } else {
    z = primitive_monte_calro(color);
  }
  t = (double)(clock()+1 - st) / CLOCKS_PER_SEC;
  prt("z=%s,color=%d,moves=%d,playouts=%d, %.1f sec(%.0f po/sec),depth=%d\n",
       get_char_z(z), color, moves, all_playouts, t, all_playouts/t, depth );
  return z;   
}

// print SGF game record
void print_sgf()
{
  int i;
  prt("(;GM[1]SZ[%d]KM[%.1f]PB[]PW[]\n",B_SIZE,komi); 
  for (i=0; i<moves; i++) {
    int z = record[i];
    int y = z / WIDTH;
    int x = z - y*WIDTH;
    const char *sStone[2] = { "B", "W" };
    prt(";%s",sStone[i&1]);
    if ( z == 0 ) {
      prt("[]");
    } else {
      prt("[%c%c]",x+'a'-1, y+'a'-1 );
    }
    if ( ((i+1)%10)==0 ) prt("\n");
  }
  prt(")\n");
}

void selfplay()
{
  int color = 1;
  int z,search;
  
  for (;;) {
    if ( color == 1 ) {
      search = SEARCH_UCT; //SEARCH_PRIMITIVE;
    } else {
      search = SEARCH_UCT;
    }
    z = get_computer_move(color, search);
    add_moves(z, color);
    if ( z == 0 && moves > 1 && record[moves-2] == 0 ) break;
    if ( moves > 300 ) break;  // too long
    color = flip_color(color);
  }

  print_sgf();
}

void test_playout()
{
  flag_record_playout = 1;
  playout(1);
  print_board();
  print_sgf();
}

int get_color_char(const char c) {
  if ( tolower(c) == 'w' ) return 2;
  return 1;
}

#define STR_MAX 256
#define TOKEN_MAX 3

void gtp_loop()
{
  char str[STR_MAX];
  char sa[TOKEN_MAX][STR_MAX];
  char seps[] = " ";
  char *token;
  int x,y,z,ax, count;

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);
  for (;;) {
    if ( fgets(str, STR_MAX, stdin)==NULL ) break;
//  prt("gtp<-%s",str);
    count = 0;
    token = strtok( str, seps );
    while ( token != NULL ) {
      strcpy(sa[count], token);
      count++;
      if ( count == TOKEN_MAX ) break;
      token = strtok( NULL, seps );
    }

    if ( strstr(sa[0],"boardsize")     ) {
      int new_board_size = atoi( sa[1] );
      init_board(new_board_size);
      send_gtp("= \n\n");
    } else if ( strstr(sa[0],"clear_board")   ) {
      init_board(B_SIZE);
      send_gtp("= \n\n");
    } else if ( strstr(sa[0],"quit") ) {
      break;
    } else if ( strstr(sa[0],"protocol_version") ) {
      send_gtp("= 2\n\n");
    } else if ( strstr(sa[0],"name")          ) {
      send_gtp("= your_program_name\n\n");
    } else if ( strstr(sa[0],"version")       ) {
      send_gtp("= 0.0.1\n\n");
    } else if ( strstr(sa[0],"list_commands" ) ) {
      send_gtp("= boardsize\nclear_board\nquit\nprotocol_version\n"
               "name\nversion\nlist_commands\nkomi\ngenmove\nplay\ntime_left\ncgos-genmove_analyze\n\n");
    } else if ( strstr(sa[0],"komi") ) {
      komi = atof( sa[1] );
      send_gtp("= \n\n");
    } else if ( strstr(sa[0],"cgos-genmove_analyze") ) {
      int c = get_color_char(sa[1][0]);
      z = get_computer_move(c, SEARCH_UCT);
      add_moves(z, c);

      // {"moves": [{"move": "O15", "winrate": 0.135, "score": -11.21, "pv": "P17 O18 N17 O8 P6 N7 Q7 O6 P5"}]}
      // "ownership": "y35678961tmifdgkpwxr6575+9/zrkgglgkk1xsB78ge+8xndbPkNeb1yNBBBBBC//pfULKV99rxEBGLSXXU/paO++8+rusDDFGljrU+iNn+88+5kmDDDED99ppVep99++99gDDDFEB908fIk9+E++SdDDDDEB972ocGF+E9HSWCCBCABB9988rHEE9HHYCCCCCDBB9rvcDRMHI5sCCCDDFHIIwhdTkiI756CCCDEGKQdecYVKxl775DBCDFIMQVYXTEZQ3243CCCDEJPRDTPMHCWoy4d+AAAAAQffjBBBB222CX+++++RDgh++U++BCCCE+9++97++5898+87CDDD+9998888778889kQDCC",
      char buf[B_SIZE*B_SIZE+1] = {0};
      double a_sum = 0;
      char cp[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
      for (y=0;y<B_SIZE;y++) for (x=0;x<B_SIZE;x++) {
        int z = get_z(x+1,y+1);
        int a = area[z];
        double b = (((double)a/all_playouts)+1.0)/2.0;	// 0 <= b <= +1
        a_sum += (b - 0.5)*2.0;
//      prt("%6.3f",b);
        if ( c == 2 ) b = 1-b;
        int e = (int)(b*62.0);
        if ( e <  0 ) e = 0;
        if ( e > 62 ) e = 62;
        buf[y*B_SIZE+x] = cp[e];
//      if ( x==B_SIZE-1 ) prt("\n");
      }

      double wr = winrate;
      double sc = a_sum - komi;
      if ( c == 2 ) { wr = wr+1.0; sc = -sc; }
      send_gtp("= {\"moves\":[{\"move\":\"%s\",\"winrate\":%.3f,\"score\":%.1f}],\"ownership\":\"%s\"}\n",get_char_z(z),wr,sc,buf);
      send_gtp("play %s\n\n",get_char_z(z));

    } else if ( strstr(sa[0],"genmove") ) {
      int c = get_color_char(sa[1][0]);
      z = get_computer_move(c, SEARCH_UCT);
      add_moves(z, c);
      send_gtp("= %s\n\n",get_char_z(z));
    } else if ( strstr(sa[0],"play") ) {  // "play b c4", "play w d17"
      int c = get_color_char(sa[1][0]);
      ax = tolower( sa[2][0] );
      x = ax - 'a' + 1;
      if ( ax >= 'i' ) x--;
      y = atoi(&sa[2][1]);
      z = get_z(x, B_SIZE-y+1);
      if ( strstr(sa[2],"pass") || strstr(sa[2],"PASS") ) z = 0;  // pass
      add_moves(z, c);
      send_gtp("= \n\n");
    } else if ( strstr(sa[0],"time_left") ) {// "time_left BLACK 1715 0"
      int c = get_color_char(sa[1][0]);
      left_time[c-1] = atoi(sa[2]);
      send_gtp("= \n\n");
    } else {
      send_gtp("? unknown_command\n\n");
    }
  }
}

int main(int argc, char *argv[])
{
  if ( argc >= 2 ) {
    if ( strncmp(argv[1],"-p",2)==0 ) uct_loop = atoi(argv[1]+2);
  }

  srand( (unsigned)time( NULL ) );
  init_board(B_SIZE);

  if ( 0 ) { selfplay(); return 0; }
  if ( 0 ) { test_playout(); return 0; }

  gtp_loop();

  return 0;
}
