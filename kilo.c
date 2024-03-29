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
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define ABUF_INIT {NULL, 0}
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

struct EditorSyntax
{
    char* filetype;
    char** filematch;
    char** keywords;
    char* singleline_comment_start;
    char* multiline_comment_start;
    char* multiline_comment_end;
    int flags;
};

char* C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char* C_HL_keywords[] = {"switch", "if", "while", "for", "break", "continue", NULL};

struct EditorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        "/*",
        "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

enum EditorKey
{
    BACKSPACE = 127,
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

enum EditorHighlight
{
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT, 
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
    HL_KEYWORD1,
    HL_KEYWORD2
};

typedef struct ERow
{
    int idx;
    int size;
    int rsize;
    char* chars;
    char* render;
    unsigned char* hl;
    int hl_open_comment;
} ERow;

struct EditorConfig
{
    int cx;
    int cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    ERow* row;
    char* filename;
    char statusmsg[80];
    char dirty;
    time_t statusmsg_time;
    struct EditorSyntax* syntax;
    struct termios orig_termios;
};

struct EditorConfig E;

struct ABuf
{
    char* b;
    int len;
};

void EditorRefreshScreen();
char* EditorPrompt(char* prompt, void (*callback)(char*, int));
int EditorRowRxToCx(ERow* row, int rx);
void EditorSelectSyntaxHighlight();
void EditorUpdateRow(ERow* row);

int EditorSyntaxToColor(int hl)
{
    switch(hl)
    {
        case HL_STRING:
            return 35;
        case HL_NUMBER:
            return 31;
        case HL_MATCH:
            return 34;
        case HL_COMMENT:
            return 36;
        case HL_KEYWORD1:
            return 33;
        case HL_KEYWORD2:
            return 32;
        case HL_MLCOMMENT:
            return 36;
        default:
            return 37;
    }
}

int is_separator(int c)
{
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

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

void EditorSetStatusMessage(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

char* EditorRowsToString(int* bufLen)
{
    int totlen = 0;
    for (int i = 0; i < E.numrows; ++i)
    {
        totlen += E.row[i].size + 1;
    }

    if (bufLen)
    {
        *bufLen = totlen;
    }

    char* buf = (char*)malloc(totlen);
    char* p = buf;
    for (int i = 0; i < E.numrows; ++i)
    {
        memcpy(p, E.row[i].chars, E.row[i].size);
        p += E.row[i].size;
        *p = '\n';
        ++p;
    }

    return buf;
}

void EditorFindCallback(char* query, int key)
{
    static int last_match = -1;
    static int direction = 1;


    static int saved_hl_line;
    static unsigned char* saved_hl = NULL;

    if (saved_hl)
    {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL; 
    }

    if (key == '\r' || key == '\x1b')
    {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
    {
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP)
    {
        direction = -1;
    }
    else
    {
        last_match = -1;
    }

    if (last_match == -1)
    {
        direction = 1;
    }
    
    int current = last_match;
    for (int i = 0; i < E.numrows; ++i)
    {
        current += direction;
        if (current == -1)
        {
            current = E.numrows - 1;
        }
        else if (current == E.numrows)
        {
            current = 0; 
        }

        ERow* row = &E.row[current];
        char* match = strstr(row->render, query);
        if (match)
        {
            last_match = current;
            E.cy = current;
            E.cx = EditorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;

            saved_hl_line = current;
            saved_hl = (unsigned char*)malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void EditorFind()
{
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char* query = EditorPrompt("Search %s (ESC to cancel", EditorFindCallback);
    if (query == NULL)
    {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
    else
    {
        free(query);
    }
}

void EditorSave()
{
    if (E.filename == NULL)
    {
        E.filename = EditorPrompt("Save as : %s", NULL);
        if (E.filename == NULL)
        {
            EditorSetStatusMessage("Save abort!");
            return;
        }
        EditorSelectSyntaxHighlight();
    }

    int len;
    char* buf = EditorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != 0)
    {
        if (ftruncate(fd, len) != -1)
        {
            if (write(fd, buf, len) == len)
            {
                E.dirty = 0;
                close(fd);
                free(buf);
                EditorSetStatusMessage("%d bytes written to disk", len);
                E.dirty = 0;
                return;
            }
        }
        close(fd);
    }

    free(buf);
    EditorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
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

void EditorUpdateSyntax(ERow* row)
{
    row->hl = (unsigned char*)realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL)
    {
        return;
    }

    char** keywords = E.syntax->keywords;

    char* scs = E.syntax->singleline_comment_start;
    char* mcs = E.syntax->multiline_comment_start;
    char* mce = E.syntax->multiline_comment_end;

    int scs_len = scs? strlen(scs) : 0;
    int mcs_len = mcs? strlen(mcs) : 0;
    int mce_len = mce? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    for (int i = 0; i < row->rsize; ++i)
    {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment)
        {
            if (!strncmp(&row->render[i], scs, scs_len))
            {
                memset(&row->hl[i], HL_COMMENT, row->rsize - 1);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string)
        {
            if (in_comment)
            {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len))
                {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
            }
            else if (!strncmp(&row->render[i], mcs, mcs_len))
            {
                memset(&row->hl[i], HL_COMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS)
        {
            if (in_string)
            {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize)
                {
                    row->hl[i + 1] = HL_STRING;
                    i += 1;
                    continue;
                }
                if (c == in_string)
                {
                    in_string = 0;
                }
                prev_sep = 1;
                continue;
            }
            else
            {
                if (c == '"' || c == '\'')
                {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS)
        {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_sep == HL_NUMBER))
            {
                row->hl[i] = HL_NUMBER;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep)
        {
            int j = 0;
            for (; keywords[j] != NULL; ++j)
            {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2)
                {
                    klen -= 1;
                }

                if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen]))
                {
                    memset(&row->hl[i], kw2? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }

            if (keywords[j] != NULL)
            {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
    }

    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numrows)
        EditorUpdateRow(&E.row[row->idx + 1]);
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
    EditorUpdateSyntax(row);
}

void EditorInsertRow(int at, char* s, size_t len)
{
    if (at < 0 || at > E.numrows)
    {
        return;
    }
    E.row = (ERow*)realloc(E.row, sizeof(ERow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(ERow) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; ++j)
    {
        E.row[j].idx += 1;
    }

    E.row[at].idx = at;
    E.row[at].size = len;
    E.row[at].chars = (char*)malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    EditorUpdateRow(&E.row[at]);

    E.numrows += 1;
    E.dirty += 1;
}

void EditorInsertNewLine()
{
    if (E.cx == 0)
    {
        EditorInsertRow(E.cy, "", 0);
    }
    else
    {
        ERow* row = &E.row[E.cy];
        EditorInsertRow(E.cy + 1, &row->chars[E.cy], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        EditorUpdateRow(row);
    }
    E.cy += 1;
    E.cx = 0;
}

void EditorFreeRow(ERow* row)
{
    free(row->chars);
    free(row->render);
    free(row->hl);
}

void EditorDelRow(int at)
{
    if (at < 0 || at >= E.numrows)
    {
        return;
    }
    EditorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(ERow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; ++j)
    {
        E.row[j].idx -= 1;
    }
    E.numrows -= 1;
    E.dirty += 1;
}

void EditorRowAppendString(ERow* row, char* s, size_t len)
{
    row->chars = (char*)realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    EditorUpdateRow(row);
    E.dirty += 1;
}

void EditorRowInsertChar(ERow* row, int at, int c)
{
    if (at < 0 || at > row->size)
    {
        at = row->size;
    }

    row->chars = (char*)realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size += 1;
    row->chars[at] = c;
    EditorUpdateRow(row);
    E.dirty += 1;
}

void EditorInsertChar(int c)
{
    if (E.cy == E.numrows)
    {
        EditorInsertRow(E.numrows, "", 0);
    }

    EditorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx += 1;
}

void EditorRowDelChar(ERow* row, int at)
{
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size -= 1;
    EditorUpdateRow(row);
    E.dirty += 1;
}

void EditorDelChar()
{
    if (E.cy >= E.numrows)
    {
        return;
    }

    if (E.cy == 0 && E.cx == 0)
    {
        return;
    }

    ERow* row = &E.row[E.cy];
    if (E.cx > 0)
    {
        EditorRowDelChar(row, E.cx - 1);
        E.cx -= 1;
    }
    else
    {
        E.cx = E.row[E.cy - 1].size;
        EditorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        EditorDelRow(E.cy);
        E.cy -= 1;
    }
}

char* EditorPrompt(char* prompt, void (*callback)(char*, int))
{
    size_t bufsize = 128;
    char* buf = (char*)malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        EditorSetStatusMessage(prompt, buf);
        EditorRefreshScreen();

        int c = EditorReadKey();
        if (c == '\r')
        {
            if (buflen != 0)
            {
                EditorSetStatusMessage("");
                if (callback)
                {
                    callback(buf, c);
                }
                return buf;
            }
        }
        else if(!iscntrl(c) && c < 128)
        {
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = (char*)realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        else if (c == '\x1b')
        {
            EditorSetStatusMessage("");
            if (callback)
            {
                callback(buf, c);
            }
            free(buf);
            return NULL;
        }
        else if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        {
            if (buflen != 0)
            {
                buf[--buflen] = '\0';
            }
        }

        if (callback)
        {
            callback(buf, c);
        }
    }
}

void EditorProcessKey()
{
    static int quit_times = KILO_QUIT_TIMES; 
    int c = EditorReadKey();

    switch (c)
    {
        case '\r':
            EditorInsertNewLine();
            break;
        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0)
            {
                EditorSetStatusMessage("WARNNING!! File has unsaved change."
                "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times -= 1;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'):
            EditorSave();
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows)
            {
                E.cx = E.row[E.cy].size;
            }
            break;
        case PAGE_UP:
        case PAGE_DOWN:
        {
            if (c == PAGE_UP)
            {
                E.cy = E.rowoff;
            }
            else if (c == PAGE_DOWN)
            {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows)
                {
                    E.cy  = E.numrows;
                }
            }
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
            
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY)
            {
                EditorMoveKey(ARROW_RIGHT);
            }
            EditorDelChar();
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;
        case CTRL_KEY('f'):
            EditorFind();
            break;
        default:
            EditorInsertChar(c);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
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

            char* c = &E.row[filerow].render[E.coloff];
            unsigned char* hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1;
            for (int j = 0; j < len; ++j)
            {
                if (iscntrl(c[j]))
                {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    AbAppend(aBuf, "\x1b[7m", 4);
                    AbAppend(aBuf, &sym, 1);
                    AbAppend(aBuf, "\x1b[m", 3);
                    if (current_color != -1)
                    {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        AbAppend(aBuf, buf, clen);
                    }
                }
                else if (hl[j] == HL_NORMAL)
                {
                    if (current_color != -1)
                    {
                        current_color = -1;
                        AbAppend(aBuf, "\x1b[39m", 5);
                    }
                    AbAppend(aBuf, &c[j], 1);
                }
                else
                {
                    int color = EditorSyntaxToColor(hl[j]);
                    if (current_color != color)
                    {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        AbAppend(aBuf, buf, clen);
                    }
                    AbAppend(aBuf, &c[j], 1);
                }
            }
            AbAppend(aBuf, "\x1b[39m", 5);
        }

        AbAppend(aBuf, "\x1b[K", 3);
        AbAppend(aBuf, "\r\n", 2);
    }
}

int EditorRowCxToRx(ERow* row, int cx)
{
    int rx = 0;
    
    for (int j = 0; j < cx; ++j)
    {
        if (row->chars[j] == '\t')
        {
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        }
        ++rx;
    }

    return rx;
}

int EditorRowRxToCx(ERow* row, int rx)
{
    int cur_rx = 0;
    
    int cx = 0;
    for (; cx < row->size; ++cx)
    {
        if (row->chars[cx] == '\t')
        {
            cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
        }
        cur_rx += 1;

        if (cur_rx > rx)
        {
            return cx;
        }
    }

    return cx;
}

void EditorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows)
    {
        E.rx = EditorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    if (E.rx < E.coloff)
    {   
        E.coloff = E.rx;
    }

    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void EditorDrawStatusBar(struct ABuf* ab)
{
    AbAppend(ab, "\x1b[7m", 4);
    
    char status[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]",
            E.numrows, E.dirty? "(modified)" : "");
    if (len > E.screencols)
    {       
        len = E.screencols;
    }

    char rstatus[80];
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
    AbAppend(ab, status, len);

    while (len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            AbAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            AbAppend(ab, " ", 1);
            len += 1;
        }
    }
    AbAppend(ab, "\x1b[m", 3);
    AbAppend(ab, "\r\n", 2);
}

void EditorDrawMessageBar(struct ABuf* ab)
{
    AbAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
    {
        msglen = E.screencols;
    }
    if (msglen && time(NULL) - E.statusmsg_time < 5)
    {
        AbAppend(ab, E.statusmsg, msglen);
    }
}

void EditorRefreshScreen()
{
    EditorScroll();

    struct ABuf aBuf = ABUF_INIT;

    AbAppend(&aBuf, "\x1b[?25l", 6);
    AbAppend(&aBuf, "\x1b[H", 3);

    EditorDrawRows(&aBuf);
    EditorDrawStatusBar(&aBuf);
    EditorDrawMessageBar(&aBuf);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1, E.rx - E.coloff + 1);
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

void EditorOpen(const char* filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    EditorSelectSyntaxHighlight();

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
        EditorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void InitEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;
    E.syntax = NULL;

    if (GetWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        Die("get windows size");
    }
    E.screenrows -= 2;
}

void EditorSelectSyntaxHighlight()
{
    E.syntax = NULL;
    if (E.filename == NULL)
    {
        return;
    }

    char* ext = strrchr(E.filename, '.');

    for (unsigned int i = 0; i < HLDB_ENTRIES; ++i)
    {
        struct EditorSyntax* s = &HLDB[i];

        unsigned int j = 0;
        while (s->filematch[j])
        {
            int is_ext = (s->filematch[j][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[j])) ||
                (!is_ext && strstr(E.filename, s->filematch[j])))
            {
                E.syntax = s;

                for (int n = 0; n < E.numrows; ++n)
                {
                    EditorUpdateRow(&E.row[n]);
                }

                return;
            }

            ++j;
        }
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

    EditorSetStatusMessage("HELO: CTRL-Q = quit | CTRL-S = save | CTRL-F = find");

    while (1)
    {
        EditorRefreshScreen();
        EditorProcessKey();
    }
    return 0;
}
