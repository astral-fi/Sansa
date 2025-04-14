/*****INCLUDES*****/

#define _DEFAULT_SOURCE
#define  _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <errno.h>
#include <iso646.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/ioctl.h>
#include <stdio.h> 
#include <string.h> 


/*****DEFINE*****/ 
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define SANSA_VERSION "0.0.2"
#define TAB_STOP 8


enum editorKey{
    ARROW_LEFT = 'd',
    ARROW_RIGHT = 'f',
    ARROW_UP = 'k',
    ARROW_DOWN = 'j'
};


/*****DATA*****/
typedef struct erow{
    int size;
    int rsize;
    char *chars;
    char *render;
}erow;




struct editorConfig{
    int cx, cy, rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencolumns;
    int numrows;
    erow *row;
    struct termios originalTerm;
    char *filename;
};

struct editorConfig term;

/*****TERMINAL*****/

void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4); //USED TO CLEAR THE SCREEN, FOR DETAILS LOOK FOR VT100 GUIDE
    write(STDOUT_FILENO, "\x1b[H", 2); //REPOSITION OF THE CURSOR TO THE TOP OF THE FILE                                
    perror(s);
    exit(1);
}
void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.originalTerm) == -1) die("tcsetttr");
}


void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &term.originalTerm) == -1) die("tcgetattr");
    atexit(disableRawMode);
    struct termios raw = term.originalTerm; //Termios contains terminal attributes 
    
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    //BRKINT stops sigint signal to break the program like ctrl+c 
    //Turns off Ctrl-S & Ctrl-Q signals.
    //Turn Ctrl+M into carriage return instead of turning into newline
   

    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); //Flip the bit of the ECHO Attribute by turning it off
    //ICANON let us read byte-by-byte instead of line-by-line 
    //ISIG turns off ctrl-z and ctrl-c signals
    //IEXTEN disables ctrl-v flag
    

    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    //Turnoff output processing; doesn't change "\n" into "\r\n" 

    raw.c_cc[VMIN] = 0; //No. of bytes to be read before read can return
    raw.c_cc[VTIME] = 1; //MAX time to wait before read can return

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); //It sets the attribute of the terminal
    //It waits for all the pending output to be written to the terminal.

    
}

char editorKeyReader(){
    int nread;
    char keyPress;
    while((nread  = read(STDIN_FILENO, &keyPress, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if(keyPress == '\x1b'){
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if(seq[0] == '['){
            switch(seq[1]){
                case 'A' : return ARROW_UP;
                case 'B' : return ARROW_DOWN;
                case 'C' : return ARROW_RIGHT;
                case 'D' : return ARROW_LEFT;
            }
        }
    
        return '\x1b';
    }
    else{
        return keyPress;
    }
}

int getCursorPosition(int *rows, int *columns){
    char buf[32];
    unsigned int i = 0;
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; //GET CURSOR POSITION
    
    while(i < sizeof(buf) - 1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break; //GETS CURSOR POSITION INTO THE BUFFER
        if(buf[i] == 'R') break;
        i++;
    }
    
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, columns) != 2) return -1; //LOADS CURSOR POSITION INTO ROW AND COLUMNS
    return 0;
}




int getWindowSize(int *rows, int *columns){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; //moves cursor to bottom right
        return getCursorPosition(rows, columns);
    }
    else{
        *columns = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/*****ROWOPS*****/ 
int rowCXtoRX(erow *row, int cx){
    int rx = 0;
    for(int i = 0; i < cx; i++){
        if(row->chars[i] == '\t'){
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        }
        rx++;
    }
    
    return rx;
}


void rowUpdate(erow *row){
    int tabs = 0;
    for(int i = 0; i < row->size; i++){
        if(row->chars[i] == '\t') tabs++;
    }
    free(row->render);
    row->render = malloc(row->size + 1 + tabs*(TAB_STOP - 1)) ;
    
    int idx = 0;
    for(int i = 0; i < row->size; i++){
        if(row->chars[i] == '\t'){
            row->render[idx++] = ' ';
            while(idx % TAB_STOP != 0) row->render[idx++] = ' ';
        }
        else{
            row->render[idx++] = row->chars[i];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

}


void rowAppend(char *line, ssize_t linelen){
        term.row = realloc(term.row, sizeof(erow) * (term.numrows + 1));
        
        int at = term.numrows;
        term.row[at].size = linelen;
        term.row[at].chars = malloc(linelen + 1);
        memcpy(term.row[at].chars, line, linelen);
        term.row[at].chars[linelen] = '\0';
        term.numrows++;
        term.row[at].rsize = 0;
        term.row[at].render = NULL;
        rowUpdate(&term.row[at]);

}
/*****FILEIO*****/ 

void editorOpen(char *filename){
    free(term.filename);
    term.filename = strdup(filename);
    FILE *file = fopen(filename, "r");
    if (!file) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen = 0;
    
    while((linelen = getline(&line, &linecap, file)) != -1){
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        rowAppend(line, linelen);
    }
    
    free(line);
    fclose(file);
}



/*****BUFFER*****/ 
struct abuf{
    char *b;
    int len;
};

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);
    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}



/*****OUTPUT*****/ 
void editorScroll(){
    term.rx = term.cx;
    if(term.cy < term.numrows){
        term.rx = rowCXtoRX(&term.row[term.cy], term.cx);
    }
    if(term.cy < term.rowoff){
        term.rowoff = term.cy;
    }

    if(term.cy >= term.rowoff  + term.screenrows){
        term.rowoff = term.cy - term.screenrows  + 1;
    }

    if(term.rx < term.coloff){
        term.coloff = term.rx;
    }

    if(term.rx >= term.coloff  + term.screencolumns){
        term.coloff = term.rx - term.screencolumns + 1;
    }

}

void editorDrawRows(struct abuf *ab){
    editorScroll();
    for(int y = 0; y < term.screenrows; y++){

        int filerow = y + term.rowoff;
        if(filerow >= term.numrows){
            if(term.numrows == 0 && y == term.screenrows / 3){
                char welcomemsg[80];
                int welcomelen = snprintf(welcomemsg, sizeof(welcomemsg), "SANSA --VERSION %s", SANSA_VERSION);
                if(welcomelen > term.screencolumns) welcomelen = term.screencolumns;
                int padding = (term.screencolumns - welcomelen)/2;
                if(padding){
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while(padding--){
                    abAppend(ab, " ", 1);
                }
                abAppend(ab, welcomemsg, welcomelen);
            }
            else{ 
                abAppend(ab, "~", 1);
            }
        }
        else{
            int len = term.row[filerow].rsize - term.coloff;
            if(len < 0) len = 0;
            if(len > term.screencolumns) len = term.screencolumns;
            abAppend(ab, &term.row[filerow].render[term.coloff], len);
        }
        abAppend(ab, "\x1b[K", 4); //USED TO CLEAR THE LINE, FOR DETAILS LOOK FOR VT100 GUIDE
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab){
    abAppend(ab, "\x1b[7m", 4); //TEXT FORMATTING FOR STATUS BAR
    
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d Lines", term.filename ? term.filename : "[No Name]", term.numrows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", term.cy + 1, term.numrows); 
    if(len > term.screencolumns) len = term.screencolumns;
    abAppend(ab, status, len);
    while(len < term.screencolumns){
        if(term.screencolumns - len == rlen){
            abAppend(ab, rstatus, rlen);
            break;
        }
        else{
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); //HIDES THE CURSOR BEFORE REFRESHING
    abAppend(&ab, "\x1b[H", 3); //REPOSITION OF THE CURSOR TO THE TOP OF THE FILE                                

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", term.cy - term.rowoff + 1, term.rx - term.coloff + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); //REAPPERS THE CURSOR BEFORE REFRESHING
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);

}


/*****INPUT*****/ 

void moveCusor(char keyPress){
    erow *row = (term.cy >= term.numrows) ? NULL : &term.row[term.cy];
    switch (keyPress) {
        case ARROW_DOWN:
            if(term.cy < term.numrows) term.cy++;
           break;
        case ARROW_UP:
            if(term.cy != 0) term.cy--;
           break;
        case ARROW_RIGHT:
           if(row && term.cx < row->size) term.cx++;
           else if(term.cy < term.numrows - 1){
               term.cy++;
               term.cx = 0;
           }
           break;
        case ARROW_LEFT:
           if(term.cx != 0) term.cx--;
           else if(term.cy > 0){
               term.cy--;
               term.cx = term.row[term.cy].size;
           }
           break;
    }

    row = (term.cy >= term.numrows) ? NULL : &term.row[term.cy];
    int rowlen = row ? row->size : 0;
    if(term.cx > rowlen){
        term.cx = rowlen;
    }
}


void editorProcessKeypress(){
    char keyPress = editorKeyReader();
    switch(keyPress){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case 'd':
        case 'f':
        case 'j':
        case 'k':
            moveCusor(keyPress);
            break;
    }
}



/*****INIT******/

void initEditor(){
    term.cx = 0;
    term.cy = 0;
    term.numrows = 0;
    term.row = NULL;
    term.rowoff = 0;
    term.coloff = 0;
    term.rx = 0;
    if(getWindowSize(&term.screenrows, &term.screencolumns) == -1) die("getWindowSize");
    term.screenrows--;
    term.filename = NULL;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }
    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
