/*
 *  Virtual Machine using Breakpoint Tracing
 *  Copyright (C) 2012 Bi Wu
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <ncurses.h>            /* ncurses.h includes stdio.h */
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int row, col;                   /* to store the number of rows and *
                                 * the number of colums of the screen */
int start_line = 0;
int counter = 0;
char *fname;
FILE *f;
unsigned long offset, len;
void *address;
char scr[4096];
#define lastline_x 0
#define lastline_y 23
int step = 0;

#define BTC_IOC_MAGIC 'v'

#define BTC_IOCRESET _IO(BTC_IOC_MAGIC, 0)

#define BTC_STATUS _IO(BTC_IOC_MAGIC, 1)
#define BTC_STEP _IO(BTC_IOC_MAGIC, 2)
#define BTC_BP  _IOW(BTC_IOC_MAGIC, 3, unsigned int)
#define BTC_KEYIN _IOW(BTC_IOC_MAGIC, 4, unsigned int)
#define BTC_UMOUNT _IOW(BTC_IOC_MAGIC, 5, unsigned int)
#define BTC_IOC_MAXNR 5

void
menu(void)
{
    char c;
    unsigned int addr;
    int ret;
    ret = ioctl(fileno(f), BTC_STATUS, 0);
    if (ret == 0)
        ret = ioctl(fileno(f), BTC_STATUS, 1);
    mvprintw(lastline_y, lastline_x, "%s             %d", "Paused", ret);
    timeout(-1);
    while (1) {
        noecho();
        cbreak();
        c = getch();
        switch (c) {
        case 'u':
        case 'U':
            ioctl(fileno(f), BTC_UMOUNT, 0);
            return;
        case 'r':
        case 'R':
            ioctl(fileno(f), BTC_STATUS, 1);
            return;
        case 's':
        case 'S':
            step = 1 - step;
            ioctl(fileno(f), BTC_STEP, 1);
            mvprintw(lastline_y, lastline_x, "%s %s               ",
                "Step", (step) ? "ON" : "OFF");
            break;
        case 'b':
        case 'B':
            mvprintw(lastline_y, lastline_x, "%s", "Breakpoint: ");
            echo();
            scanw("%x", &addr);
            ioctl(fileno(f), BTC_BP, addr);
            mvprintw(lastline_y, lastline_x, "%s %x               ",
                "Breakpoint on ", addr);
            break;
        default:
            mvprintw(lastline_y, lastline_x, "%s                  ",
                "Unknown command");

        }
    }

}

void
do_keys(void)
{
    struct itimerval tout_val;
    int i, j;
    int c;

    while (1) {
        counter++;
        for (i = start_line; i < 120; i++) {
            for (j = 0; j < 80; j++) {
                if (i - start_line < row && j < col) {
                    unsigned char bg = ((unsigned char *)
                        address)[(i * 80 + j) * 2 + 1];
                    //init_pair(1, bg >> 8, bg & 0xf);
                    unsigned char fg = ((unsigned char *)
                        address)[(i * 80 + j) * 2];
                    if (fg == 0) {
                        fg = ' ';       // so that it displays
                    }
                    mvprintw(i - start_line, j, "%c", fg);
                }
            }
        }
        mvprintw(row / 2, 40, "%d", counter);
        timeout(1000);
        noecho();
        keypad(stdscr, TRUE);
        c = getch();
        switch (c) {
        case KEY_HOME:
            if (start_line > 0);
            start_line -= 1;
            if (start_line < 0)
                start_line = 0;
            break;
        case KEY_END:
            start_line += 1;
            break;
        case KEY_F(5):
            menu();
            break;
        default:
            ioctl(fileno(f), BTC_KEYIN, c);
            break;
        }
        refresh();
    }
}

int
main(int argc, char **argv)
{
    struct itimerval tout_val;

    if (argc != 4
        || sscanf(argv[2], "%li", &offset) != 1
        || sscanf(argv[3], "%li", &len) != 1) {
        fprintf(stderr, "%s: Usage \"%s <file> <offset> <len>\"\n",
            argv[0], argv[0]);
        exit(1);
    }
    noecho();
    cbreak();
    timeout(0);
    keypad(stdscr, TRUE);
    if (offset == INT_MAX) {
        if (argv[2][1] == 'x')
            sscanf(argv[2] + 2, "%lx", &offset);
        else
            sscanf(argv[2], "%lu", &offset);
    }

    fname = argv[1];

    if (!(f = fopen(fname, "rw"))) {
        fprintf(stderr, "%s: %s: %s\n", argv[0], fname, strerror(errno));
        exit(1);
    }

    address =
        mmap(0, len, PROT_READ, MAP_FILE | MAP_PRIVATE, fileno(f), offset);

    if (address == (void *) -1) {
        fprintf(stderr, "%s: mmap(): %s\n", argv[0], strerror(errno));
        exit(1);
    }

    fprintf(stderr, "mapped \"%s\" from %lu (0x%08lx) to %lu (0x%08lx)\n",
        fname, offset, offset, offset + len, offset + len);
    initscr();                  /* start the curses mode */
    getmaxyx(stdscr, row, col); /* get the number of rows and columns */

    do_keys();
    return 0;

}
