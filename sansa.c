/*****INCLUDES*****/

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <errno.h>
#include <iso646.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <stdio.h> 
#include <string.h> 


/*****DEFINE*****/ 
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define SANSA_VERSION "0.0.1"

/*****DATA*****/

struct editorConfig{
    int cx, cy;
    int screenrows;
    int screencolumns;
    struct termios originalTerm;
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
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return keyPress;
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

void editorDrawRows(struct abuf *ab){
    for(int y = 0; y < term.screenrows; y++){
        if(y == term.screenrows / 3){
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
        abAppend(ab, "\x1b[K", 4); //USED TO CLEAR THE LINE, FOR DETAILS LOOK FOR VT100 GUIDE
        if(y < term.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); //HIDES THE CURSOR BEFORE REFRESHING
    abAppend(&ab, "\x1b[H", 3); //REPOSITION OF THE CURSOR TO THE TOP OF THE FILE                                

    editorDrawRows(&ab);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", term.cy + 1, term.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); //REAPPERS THE CURSOR BEFORE REFRESHING
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);

}


/*****INPUT*****/ 

void moveCusor(char keyPress){
    switch (keyPress) {
        case 'j':
           term.cy++;
           break;
        case 'k':
           term.cy--;
           break;
        case 'f':
           term.cx++;
           break;
        case 'd':
           term.cx--;
           break;
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
    if(getWindowSize(&term.screenrows, &term.screencolumns) == - 1) die("getWindowSize");
}

int main(){
    enableRawMode();
    initEditor();
    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
