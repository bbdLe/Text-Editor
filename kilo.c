#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define ABUF_INIT {NULL, 0}
#define KILO_TAB_STOP 8

enum EditorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

typedef struct ERow
{
    int size;
    int rsize;
    char* chars;
    char* render;
} ERow;

struct EditorConfig
{
    int cx;
    int cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    ERow* row;
    struct termios orig_termios;
};

struct EditorConfig E;

struct ABuf
{
    char* b;
    int len;
};

void AbAppend(struct ABuf* ab, const char* s, int len)
{
    char* newBuf = (char*)(realloc(ab->b, ab->len + len));

    if (newBuf == NULL)
    {
        return;
    }
    memcpy(&newBuf[ab->len], s, len);
    ab->b = newBuf;
    ab->len += len;
}

void AbFree(struct ABuf* ab)
{
    free(ab->b);
}

void Die(const char* s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void DisableRawModel()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    {
        Die("tcsetattr");
    }
}

void EnableRawModel()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    {
        Die("tcsgetattr");
    }
    atexit(DisableRawModel);
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        Die("tcsetattr");
    }
}

int EditorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            Die("read");
    }

    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
        {
            return '\x1b';
        }

        if (read(STDIN_FILENO, &seq[1], 1) != 1)
        {
            return '\x1b';
        }

        if (seq[0] == '[')
        {
            if (seq[1] > '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                {
                    return '\x1b';
                }
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                       return ARROW_LEFT;
                    case 'H':
                       return HOME_KEY;
                    case 'F':
                       return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch(seq[1])
            {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    }
    else
    {
        return c;
    }
}

void EditorMoveKey(int key)
{
    ERow* row = (E.cy >= E.numrows)? NULL : &E.row[E.cy];

    switch(key)
    {
        case ARROW_LEFT: 
            if (E.cx != 0)
            {
                E.cx -= 1; 
            }
            else if (E.cy != 0)
            {
                E.cy -= 1;
                E.cx = E.row[E.cy].size;
            }
            break; 
        case ARROW_RIGHT:
            if (row && row->size > E.cx)
            {
                E.cx += 1;
            }
            else if (row && E.cx == row->size)
            {
                E.cy += 1;
                E.cx = 0;
            }
            break;
        case ARROW_UP: 
            if (E.cy != 0)
            {
                E.cy -= 1;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.numrows)
            {
                E.cy += 1;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

void EditorProcessKey()
{
    int c = EditorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
        {
            int iTimes = E.screenrows;
            while(--iTimes)
            {
                EditorMoveKey(c == PAGE_UP? ARROW_UP : ARROW_DOWN);
            }
        }
            break;
        case ARROW_UP: 
        case ARROW_DOWN: 
        case ARROW_LEFT: 
        case ARROW_RIGHT: 
            EditorMoveKey(c);
            break;
    }
}

void EditorDrawRows(struct ABuf* aBuf)
{
    int y;

    for (y = 0; y < E.screenrows; ++y)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];

                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                {
                    welcomelen = E.screencols;
                }

                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    AbAppend(aBuf, "~", 1);
                    --padding;
                }

                while (padding--)
                {
                    AbAppend(aBuf, " ", 1);
                }

                AbAppend(aBuf, welcome, welcomelen);
            }
            else
            {
                AbAppend(aBuf, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
            {
                len = 0;
            }
            if (len > E.screencols)
            {
                len = E.screencols;
            }
            AbAppend(aBuf, &E.row[filerow].render[E.coloff], len);
        }

        AbAppend(aBuf, "\x1b[K", 3);
        if (y < E.screenrows - 1)
        {
            AbAppend(aBuf, "\r\n", 2);
        }
    }
}

void EditorScroll()
{
    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    if (E.cx < E.coloff)
    {   
        E.coloff = E.cx;
    }

    if (E.cx >= E.coloff + E.screencols)
    {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void EditorRefreshScreen()
{
    EditorScroll();

    struct ABuf aBuf = ABUF_INIT;

    AbAppend(&aBuf, "\x1b[?25l", 6);
    AbAppend(&aBuf, "\x1b[H", 3);

    EditorDrawRows(&aBuf);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1, E.cx - E.coloff + 1);
    AbAppend(&aBuf, buf, strlen(buf));

    AbAppend(&aBuf, "\x1b[?25h", 6);
    write(STDOUT_FILENO, aBuf.b, aBuf.len);
    AbFree(&aBuf);
}

int GetCursorPosition(int* rows, int* cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    {
        return -1;
    }

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
        {
            break;
        }

        if (buf[i] == 'R')
        {
           break; 
        }

        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
    {
        return -1;
    }
    
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    {
        return -1;
    }

    return 0;
}

int GetWindowSize(int* rows, int* cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
        {
            return -1;
        }
        return GetCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void EditorUpdateRow(ERow* row)
{
    int tabs = 0;
    for (int j = 0; j < row->size; ++j)
    {
        if (row->chars[j] == '\t')
        {
            tabs += 1;
        }
    }

    free(row->render);
    row->render = (char*)malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (int j = 0; j < row->size; ++j)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0)
            {
                row->render[idx++] = ' ';
            }
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void EditorAppendRow(char* s, size_t len)
{
    E.row = (ERow*)realloc(E.row, sizeof(ERow) * (E.numrows + 1));

    E.row[E.numrows].size = len;
    E.row[E.numrows].chars = (char*)malloc(len + 1);
    memcpy(E.row[E.numrows].chars, s, len);
    E.row[E.numrows].chars[len] = '\0';

    E.row[E.numrows].rsize = 0;
    E.row[E.numrows].render = NULL;
    EditorUpdateRow(&E.row[E.numrows]);

    E.numrows += 1;
}

void EditorOpen(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp)
    {
        Die("Open");
    }


    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while(linelen > 0 && (line[linelen - 1] == '\r' || line[linelen - 1] == '\n'))
        {
            linelen -= 1;
        }
        EditorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}


void InitEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;

    if (GetWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        Die("get windows size");
    }
}

int main(int argc, char** argv)
{
    EnableRawModel();
    InitEditor();
    if (argc >=2 )
    {
        EditorOpen(argv[1]);
    }

    while (1)
    {
        EditorRefreshScreen();
        EditorProcessKey();
    }
    return 0;
}
