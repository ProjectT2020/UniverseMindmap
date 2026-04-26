#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <termios.h>
#include <wchar.h>
#include <locale.h>
#include <termios.h>

#include "input_state.h"
#include "tty.h"
#include "ui.h"
#include "../app/app.h"
#include "../utils/logging.h"
#include "../operate/operate_type.h"
#include "../layout/mindmap_layout.h"

UserOperation * uo_create(UserOperation uo){
    UserOperation *new_uo = malloc(sizeof(UserOperation));
    if(new_uo == NULL){
        log_error("Failed to allocate memory for UserOperation");
        return NULL;
    }
    *new_uo = uo;
    return new_uo;
}

void uo_destroy(UserOperation *uo) {
    if (uo != NULL) {
        free(uo);
    }
}

char next_char() {
    char c = 0;
    int n = read(STDIN_FILENO, &c, 1);
    if(n == -1 && errno == EINTR){
        // Interrupted by signal, can ignore and continue
        clearerr(stdin); // Clear error to continue reading
        return next_char();
    }
    if(n <= 0){
        log_debug("next_char: read returned %d, errno=%d\n", n, errno);
        return 0; // return NUL on read error
    }
    return c;
}

UserOperation ui_poll_user_input(UiContext *ctx) {
    AppState *app = ctx->app;
    InputState *input_state = app->input_state;
    if(input_state->type == INPUT_STATE_TYPE_COMPUTED_INPUT){
        UserOperation *uo = queue_dequeue(input_state->uo_queue);
        if(uo == NULL){
            log_error("Expected a UserOperation in the queue but got NULL");
            return (UserOperation){.type = UO_NOP};
        }
        UserOperation result = *uo;
        free(uo);
        if(queue_is_empty(input_state->uo_queue)){
            input_state->type = INPUT_STATE_DEFAULT;
        }
        return result;
    }
    if(input_state->type == INPUT_STATE_TYPE_SEARCH_BACKWARD_QUERY){
        UserOperation uo = {.type = UO_DO_SEARCH_BACKWARD};
        input_state->type = INPUT_STATE_DEFAULT; // reset to default after getting command
        return uo;
    }
    if(input_state->type == INPUT_STATE_TYPE_GET_COMMAND){
        UserOperation uo = {.type = UO_DO_COMMAND};
        input_state->type = INPUT_STATE_DEFAULT; // reset to default after getting command
        return uo;
    }
    UserOperation input;
    bool in_paste = false;
    memset(&input, 0, sizeof(input));  // Initialize to zero
    char c = next_char();
    log_debug("[ui] User input: %c (0x%02x)", (c >= 32 && c <= 126) ? c : ' ', c);
    switch(c) {
        case 'h':
            input.type = UO_MOVE_FOCUS_LEFT;
            break;
        case 'j':
            input.type = UO_MOVE_FOCUS_NEXT_SIBLING;
            break;
        case 'k':
            input.type = UO_MOVE_FOCUS_PREV_SIBLING;
            break;
        case 'l':
            input.type = UO_MOVE_FOCUS_RIGHT;
            break;
        case 'e':
            input.type = UO_MOVE_FOCUS_LAST_CHILD;
            break;
        case 'm': {
            char next = next_char();
            if('a' <= next && next <= 'z' || 'A' <= next && next <= 'Z' || '0' <= next && next <= '9'){
                input.type = UO_MARK_NODE;
                input.param1 = next;
            }else if(next == '['){
                input.type = UO_MARK_AS_DEFINITION;
            }else if(next == ']'){
                input.type = UO_UNMARK_AS_DEFINITION;
            }else {
                log_info("invalid mark character: %c\n", next);
            }
            break;
        }
        case '\'':{
            char next = next_char();
            if('a' <= next && next <= 'z' || 'A' <= next && next <= 'Z' || '0' <= next && next <= '9'){
                input.type = UO_JUMP_TO_MARK;
                input.param1 = next;
            } else {
                log_info("invalid mark character for jump: %c\n", next);
            }
            break;
        }
        case '^':
            input.type = UO_MOVE_FOCUS_TERM_ROOT;  // first ancestor with [] definition
            break;
        case '$':
            input.type = UO_MOVE_FOCUS_MOST_LEFT_UPPER;
            break;
        case 'E':
            input.type = UO_MOVE_FOCUS_MOST_LEFT_LOWER;
            break;
        case '0':
            input.type = UO_MOVE_FOCUS_HOME;
            break;
        case 'J':
            input.type = UO_JOIN_SIBLING_AS_CHILD;
            break;
        case 'x': 
            input.type = UO_CUT_NODE;
            break;
        case 'z': {
            char next = next_char();
            switch(next) {
            case 'c':
                input.type = UO_FOLD_NODE;      // zc fold current node
                break;
            case 'r':
                input.type = UO_REDUCE_FOLDING;    // zr expand more levels (reveal more)
                break;
            case 'o':
                input.type = UO_UNFOLD_NODE;    // zo expand current node
                break;
            case 'm':
                input.type = UO_FOLD_MORE;  // zm fold more (show less)
                break;
            case 'M':
                input.type = UO_FOLD_LEVEL_1;  // zM fold to level 1
                break;
            case 'R':
                input.type = UO_EXPAND_ALL_DESCENDANTS; // zR expand all descendants except .meta
                break;
            case 'T':
                input.type = UO_INDEX_FROM_ROOT;
                break;
            case 0x14: // Ctrl+T
                // input.type = ;
                break;
            case '.':
                input.type = UO_CENTER_VIEW;    // z. center current node
                break;
            case 's':
                input.type = UO_PLACE_LEFT;    // zs current node on the left
                break;
            case 'e':
                input.type = UO_PLACE_RIGHT;    // zs current node on the left
                break;
            case 'H':
                input.type = UO_VIEW_HALF_SCREEN_LEFT;
                break;
            case 'L':
                input.type = UO_VIEW_HALF_SCREEN_RIGHT;
                break;
            default:
                log_info("Unknown input sequence: z%c\n", next);
            }
            break;
        }
        case 'a':
            input.type = UO_EDIT_NODE_END;
            break;
        case '\t': // TAB or ^I 
            if(ctx->last_input.type == UO_JUMP_BACK || ctx->last_input.type == UO_JUMP_FORWARD){
                input.type = UO_JUMP_FORWARD;
            } else {
                input.type = UO_ADD_CHILD_TO_TAIL;
            }
            break;
        case 'o':
            input.type = UO_ADD_SIBLING_BELOW;
            break;
        case ' ':
            input.type = UO_HIT_SPACE;
            break;
        case '\n':
            input.type = UO_HIT_CTRL_J;
            break;
        case '\r':
            input.type = UO_HIT_ENTER;
            break;
        case 'A':
            input.type = UO_ADD_CHILD_TO_TAIL;
            break;
        case 'O':
            input.type = UO_ADD_SIBLING_ABOVE;
            break;
        case 's':
            input.type = UO_EDIT_NODE;
            break;
        case 'i':
            input.type = UO_EDIT_NODE_FRONT;
            break;
        case 'I':
            input.type = UO_INSERT_PARENT_LEFT;
            break;
        case 'D':
            input.type = UO_DELETE_SUBTREE;
            break;
        case 'd': {
            char next = next_char();
            if (next == 'd') {
                input.type = UO_CUT_SUBTREE;
            } else {
                log_info("Unknown input sequence: d%c\n", next);
            }
            break;
        }
        case 'K':
            input.type = UO_KEYWORD_LOOKUP;
            break;
            
        case 'u':
            input.type = UO_UNDO;
            break;
        case 18: // Ctrl+R - redo
            input.type = UO_REDO;
            break;
        case '/':
            input.type = UO_SEARCH;
            break;
        case 'n': {
            // Check if 'n' means search next (only in some contexts)
            // For now, treat 'n' as search next if user has a query
            input.type = UO_SEARCH_NEXT;
            break;
        }
        case 'N': {
            input.type = UO_SEARCH_PREV;
            break;
        }
        case '*':
            input.type = UO_SEARCH_NEXT_EXACT;
            break;
        case '#':
            input.type = UO_SEARCH_PREV_EXACT;
            break;
        case 15: // Ctrl+O - jump back
            input.type = UO_JUMP_BACK;
            break;
        case 0x1d: // Ctrl+]
            input.type = UO_JUMP_KEYWORD_GLOBAL_DEFINITION;
            break;
        case 'y':
            input.type = UO_COPY_SUBTREE;
            break;
        case 'p':
            input.type = UO_PASTE_SIBLING_BELOW;
            break;
        case 'P':
            input.type = UO_PASTE_SIBLING_ABOVE;
            break;
        case 'T': {
            app->show_child_position = true;
            ui_render(ctx);
            char next = next_char();
            int pos = 0;
            if('0' <= next && next <= '9'){
                pos = next - '0';
            } else if('a' <= next && next <= 'z'){
                pos = next - 'a' + 10; // 10-35 for a-z
            } else if('A' <= next && next <= 'Z'){
                pos = next - 'A' + 36; // 36-61 for A-Z
            } else {
                log_info("Unknown input sequence: f%c\n", next);
                break;
            }
            input.type = UO_MOVE_TO_CHILD_POSITION;
            input.param1 = pos;
            break;
        }
        case 't':{
            app->input_state->mark_and_show_visible_nodes = true;
            ctx->mark_page = 0;
            ctx->mark = 0;
            ctx->app->fix_view = true;
            ui_render(ctx);
            
            char next = next_char();
            if('a' <= next && next <= 'z'){
                char next2 = next_char();
                if('a' <= next2 && next2 <= 'z'){
                    input.type = UO_JUMP_TO_UI_NODE_MARK;
                    input.param1 = (next - 'a') * 26 + (next2 - 'a');
                } else {
                    log_info("Unknown input sequence: f%c%c\n", next, next2);
                }
            } else {
                app->input_state->mark_and_show_visible_nodes = false;
                ctx->app->fix_view = false;
                log_info("Unknown input sequence: f%c\n", next);
            }

            break;
        }
        case 'v':
            input.type = UO_VI_EDIT_NODE;
            break;
        case 0x07: // Ctrl+G
            ctx->show_ancestors_in_one_line = !ctx->show_ancestors_in_one_line;
            break;
        case 'g': {
            char next = next_char();
            switch(next) {
                case 'p':
                    input.type = UO_PASTE_AS_CHILD;
                    break;
                case '0':
                    input.type = UO_MOVE_FOCUS_HOME;
                    break;
                case 'c':
                    input.type = UO_MOVE_FOCUS_CURRENT_TASK;
                    break;
                case 'j':
                    input.type = UO_MOVE_FOCUS_DOWN;
                    break;
                case 'k':
                    input.type = UO_MOVE_FOCUS_UP;
                    break;
                case 'g':
                    input.type = UO_MOVE_FOCUS_TOP;
                    break;
                case 'J':
                    input.type = UO_JOIN_TEXT_WITHOUT_SPACE;
                    break;
                case 'y':
                    input.type = UO_COPY_TEXT_TO_SYSTEM_CLIPBOARD;
                    break;
                case 'Y':
                    input.type = UO_COPY_SUBTREE_TO_SYSTEM_CLIPBOARD;
                    break;
                case 'd':
                    input.type = UO_JUMP_KEYWORD_DEFINITION;
                    break;
                case 'D':
                    input.type = UO_OPEN_RESOURCE_LINK;
                    break;
                case 'f':
                    input.type = UO_OPEN_RESOURCE_LINK;
                    break;
                case ';':
                    input.type = UO_TO_EDIT_HISTORY;
                    break;
                default:
                    log_info("Unknown input sequence: g%c\n", next);
                    break;
            }
            break;
        }
        case 'G':
            input.type = UO_MOVE_FOCUS_BOTTOM;
            break;
        case 'Z': {
            log_debug("Detected 'Z' input, checking next character for exit command\n");
            char next1 = next_char();
            if (next1 == 'Q') {
                input.type = UO_EXIT_NO_SAVE;
            } else if (next1 == 'Z') {
                input.type = UO_EXIT_SAVE;
            } else {
                log_info("Unknown input sequence: Z%c\n", next1);
            }
            break;
        }
        case '\\':{
            char next1 = next_char();
            if(('0' <= next1 && next1 <= '9') || ('a' <= next1 && next1 <= 'z') || ('A' <= next1 && next1 <= 'Z')){
                char next2 = next_char();
                if(('0' <= next2 && next2 <= '9') || ('a' <= next2 && next2 <= 'z') || ('A' <= next2 && next2 <= 'Z')){
                    char two_char_command[3] = {next1, next2, '\0'};
                    // new task
                    if(strcmp("nt", two_char_command) == 0){ 
                        // input.type = UO_NEW_TASK; // todo; we've already have :new task
                    } 
                    // current task
                    // we have gc for curent task
                    // create child task
                    else if(strcmp("ct", two_char_command) == 0){
                        input.type = UO_CREATE_CHILD_TASK;
                    }
                    // create sibling task
                    else if(strcmp("st", two_char_command) == 0){
                        input.type = UO_CREATE_SIBLING_TASK;
                    }
                    // finish task
                    else if(strcmp("ft", two_char_command) == 0){ 
                         input.type = UO_FINISH_TASK;
                    } 
                    // as current task
                    else if(strcmp("ac", two_char_command) == 0){ 
                        input.type = UO_AS_CURRENT_TASK;
                    }
                    else if(strcmp("cd", two_char_command) == 0){
                        input.type = UO_CURRENT_TASK_JUMP_DEFINITION;
                    }
                    else {
                        log_info("Unknown two-character command: \\%s\n", two_char_command);
                    }
                }
            }
            break;
        }
        case '{':
            input.type = UO_MOVE_PARENT_PREV_SIBLING_BEGIN;
            break;
        case '}':
            input.type = UO_MOVE_PARENT_NEXT_SIBLING_END;
            break;
        case '[':{
            char next = next_char();
            switch(next){
                case '[':
                    input.type = UO_MOVE_PARENT_PREV_SIBLING_BEGIN;
                    break;
                case '{':
                    input.type = UO_MOVE_FOLD_BEGIN;
                    break;
                case ']':
                    input.type = UO_MOVE_PARENT_PREV_SIBLING_END;
                    break;
                case 't':
                    input.type = UO_PREV_TASK;
                    break;
                default:
                    log_info("Unknown input sequence: [%c\n", next);
                    break;
            }
            break;
        }
        case ']':{
            char next = next_char();
            switch(next){
                case ']':
                    input.type = UO_MOVE_PARENT_NEXT_SIBLING_END;
                    break;
                case '}':
                    input.type = UO_MOVE_FOLD_END;
                    break;
                case '[':
                    input.type = UO_MOVE_PARENT_NEXT_SIBLING_BEGIN;
                    break;
                case 't':
                    input.type = UO_NEXT_TASK;
                    break;
                default:
                    log_info("Unknown input sequence: ]%c\n", next);
                    break;
            }
            break;
        }
        case ':':
            input.type = UO_COMMAND_MODE;
            break;
        case '"':{
            char next1 = next_char();
            if(next1 == '*'){
                char next2 = next_char();
                if(next2 == 'y'){
                    char next3 = next_char();
                    if(next3 == 'j'){
                        input.type = UO_COPY_SUBTREE_TO_SYSTEM_CLIPBOARD;
                    } 
                }else if(next2 == 'p'){
                    input.type = UO_PASTE_FROM_SYSTEM_CLIPBOARD_AS_SIBLINGS;
                }
            }
            break;
        }
        case '?':
            input.type = UO_SEARCH_BACKWARD;
            break;
        case 0x5: // Ctrl+E
            input.type = UO_VIEW_DOWN;
            break;
        case 0x19: // Ctrl+Y
            input.type = UO_VIEW_UP;
            break;
        case 0x06: // Ctrl+F
            input.type = UO_NEXT_PAGE;
            break;
        case 0x02: // Ctrl+B
            input.type = UO_PREV_PAGE;
            break;
        case 0x1b: // ESC key
        {
            char c1 = next_char();
            switch(c1) {
                case '[':{
                    char buf[16];
                    int i = 0;
                    while(i < (int)sizeof(buf) - 1){
                        char c2 = next_char();
                        if(c2 == '~' || (c2 >= 'A' && c2 <= 'D')){
                            buf[i] = c2;
                            buf[i + 1] = '\0';
                            break;
                        }
                        buf[i++] = (char)c2;
                    }
                    // directional keys and delete key
                    if(strcmp(buf, "A") == 0){
                        input.type = UO_MOVE_FOCUS_UP;
                        break;
                    }
                    if(strcmp(buf, "B") == 0){
                        input.type = UO_MOVE_FOCUS_DOWN;
                        break;
                    }
                    if(strcmp(buf, "C") == 0){
                        input.type = UO_MOVE_FOCUS_RIGHT;
                        break;
                    }
                    if(strcmp(buf, "D") == 0){
                        input.type = UO_MOVE_FOCUS_LEFT;
                        break;
                    }
                    if(strcmp(buf, "3~") == 0){// Delete key
                        input.type = UO_CUT_SUBTREE; // treat Delete key as cut subtree
                        break;
                    }
                    if(strcmp(buf, "6~") == 0) {// Page Down key
                        input.type = UO_NEXT_PAGE;
                        break;
                    }
                    if(strcmp(buf, "5~") == 0) {// Page Up key
                        input.type = UO_PREV_PAGE;
                        break;
                    }
                    // Alt+F12
                    if(strcmp(buf, "31~") == 0 
                    || strcmp(buf, "18;2~") == 0 /* extended key mode */){
                        input.type = UO_SHELL_ABOVE;
                        break;
                    }
                    if(strcmp(buf, "200~") == 0){
                        in_paste = true;
                        log_debug("Detected start of paste sequence (200~), entering paste mode\n");
                        break;
                    }
                    if(strcmp(buf, "201~") == 0){
                        in_paste = false;
                        log_debug("Detected end of paste sequence (201~), exiting paste mode\n");
                        break;
                    }
                    break;
                }
                case 's': // Alt+s
                    input.type = UO_SEARCH_ENGINE;
                    break;
                case 'o': // Alt+o
                    input.type = UO_OPEN_RESOURCE_LINK;
                    break;
                case 'a':
                    input.type = UO_ASK_AI;
                    input.param1 = QUERY_SCOPE_CURRENT_NODE;
                    break;
                case 'A':
                    input.type = UO_ASK_AI;
                    input.param1 = QUERY_SCOPE_SUBTREE;
                    break;
                default:
                    log_debug("Unknown escape sequence: ESC [ %c\n", c1);
                    break;
            }
            break;
        }
        default:
            log_debug("Unknown input: %c\n", c);
            input.type = UO_NOP;  // Set to NOP for unknown input
            break;
    }
    ctx->last_input = input; // Update last input
    return input;
}
static void ui_show_message(UiContext *ctx, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    
    // Move cursor to bottom line
    printf("\033[%d;1H", ctx->height);
    // Clear the line
    printf("\033[2K");
    // Print the message
    printf("%s", message);
    fflush(stdout);
    
    va_end(args);
}

static size_t clamp_cursor_bytes(const char *text, size_t cursor_bytes) {
    size_t text_len = strlen(text);
    if (cursor_bytes >= text_len) {
        return text_len;
    }

    while (cursor_bytes > 0 && (((unsigned char)text[cursor_bytes]) & 0xC0) == 0x80) {
        cursor_bytes--;
    }
    return cursor_bytes;
}

static int utf8_char_width_bytes(const char *s, size_t len) {
    wchar_t wc;
    if (len == 0) {
        return 0;
    }
    mbtowc(&wc, s, len);
    {
        int w = wcwidth(wc);
        return w > 0 ? w : 1;
    }
}

static int utf8_display_width_bytes(const char *s, size_t byte_len) {
    size_t i = 0;
    int width = 0;
    while (i < byte_len && s[i] != '\0') {
        size_t char_len = (size_t)mblen(s + i, MB_CUR_MAX);
        if (char_len == (size_t)-1 || char_len == (size_t)-2 || char_len == 0) {
            char_len = 1;
        }
        if (i + char_len > byte_len) {
            break;
        }
        width += utf8_char_width_bytes(s + i, char_len);
        i += char_len;
    }
    return width;
}

static size_t utf8_next_char_end(const char *s, size_t cursor_bytes) {
    size_t text_len = strlen(s);
    if (cursor_bytes >= text_len) {
        return text_len;
    }

    {
        size_t char_len = (size_t)mblen(s + cursor_bytes, MB_CUR_MAX);
        if (char_len == (size_t)-1 || char_len == (size_t)-2 || char_len == 0) {
            char_len = 1;
        }
        if (cursor_bytes + char_len > text_len) {
            return text_len;
        }
        return cursor_bytes + char_len;
    }
}

static void edit_mode_render(
    const char *prefix,
    const char *text,
    int cursor_x,
    int cursor_y,
    size_t cursor_bytes
) {
    int prefix_width = utf8_display_width_bytes(prefix ? prefix : "", strlen(prefix ? prefix : ""));
    int text_width_to_cursor = utf8_display_width_bytes(text, cursor_bytes);

    set_cursor_position(cursor_x, cursor_y);
    printf("%s%s\033[K", prefix ? prefix : "", text);
    fflush(stdout);
    set_cursor_position(cursor_x + prefix_width + text_width_to_cursor, cursor_y);
}

char* edit_mode(
    UiContext *ctx,
    const char *prefix,
    const char *initial_text,
    size_t initial_cursor_bytes,
    int cursor_x,
    int cursor_y,
    char *out_terminated_character
){
    struct termios tty_mode_saved;
    tty_edit_mode(&tty_mode_saved);
    show_cursor();
    cursor_blink();

    #define MAX_WIDTH  1024
    static char new_name[MAX_WIDTH];
    snprintf(new_name, sizeof(new_name), "%s", initial_text ? initial_text : "");
    size_t cursor_bytes = clamp_cursor_bytes(new_name, initial_cursor_bytes);
    edit_mode_render(prefix, new_name, cursor_x, cursor_y, cursor_bytes);
    int c;
    bool in_paste = false;
    while(1) {
        c = next_char();
        if(c == EOF) {
            if (feof(stdin)) {
                printf("EOF (end of file)\n");
            } else if (ferror(stdin)) {
                if(errno == EINTR){
                    // Interrupted by signal, can ignore and continue
                    clearerr(stdin); // Clear error to continue reading
                    continue;
                }
                char * error_msg = strerror(errno);
                printf("Read error: %s\n", error_msg);
                log_error("edit_mode: Read error: %s", error_msg);
                clearerr(stdin); // clear error status for next read
            }
        }
        if(c == 0x1b){ // ESC
            log_debug("ESC pressed, checking for escape sequence or single ESC\n");
            char c1 = next_char();
            if(c1 == '['){
                char buf[16];
                int i = 0;
                while(i < (int)sizeof(buf) - 1){
                    char c2 = next_char();
                    if(c2 == '~' || (c2 >= 'A' && c2 <= 'D')){
                        buf[i] = '\0';
                        break;
                    }
                    buf[i++] = (char)c2;
                }
                if(strcmp(buf, "200") == 0){// paste start
                    in_paste = true;
                    continue;
                }
                if(strcmp(buf, "201") == 0){// paste end
                    in_paste = false;
                    continue;
                }
                if(strcmp(buf, "D") == 0){
                    if(cursor_bytes > 0){
                        cursor_bytes = (size_t)utf8_prev_char_start(new_name, (int)cursor_bytes);
                        edit_mode_render(prefix, new_name, cursor_x, cursor_y, cursor_bytes);
                    }
                    continue;
                }
                if(strcmp(buf, "C") == 0){
                    cursor_bytes = utf8_next_char_end(new_name, cursor_bytes);
                    edit_mode_render(prefix, new_name, cursor_x, cursor_y, cursor_bytes);
                    continue;
                }
            } // '['
            else {
                // single ESC pressed?
                log_warn("should not reach here\n");
                if(out_terminated_character){
                    *out_terminated_character = (char)c;
                }
                break;
            }
        } // ESC
        if(!in_paste && 
            (c == EOF || c == '\n' || c == '\t')){
            if(out_terminated_character){
                *out_terminated_character = (char)c;
            }
            break; // end of input line
        }
        if(c == 127 || c == 8) { // backspace
            if (cursor_bytes > 0) {
                size_t prev_start = (size_t)utf8_prev_char_start(new_name, (int)cursor_bytes);
                memmove(new_name + prev_start, new_name + cursor_bytes, strlen(new_name + cursor_bytes) + 1);
                cursor_bytes = prev_start;
                edit_mode_render(prefix, new_name, cursor_x, cursor_y, cursor_bytes);
            }
            continue;
        } else if(c == 0x17){// Ctrl+W; delete last word
            while(cursor_bytes > 0 && new_name[cursor_bytes - 1] == ' '){
                size_t prev_start = (size_t)utf8_prev_char_start(new_name, (int)cursor_bytes);
                memmove(new_name + prev_start, new_name + cursor_bytes, strlen(new_name + cursor_bytes) + 1);
                cursor_bytes = prev_start;
            }
            while(cursor_bytes > 0 && new_name[cursor_bytes - 1] != ' '){
                size_t prev_start = (size_t)utf8_prev_char_start(new_name, (int)cursor_bytes);
                memmove(new_name + prev_start, new_name + cursor_bytes, strlen(new_name + cursor_bytes) + 1);
                cursor_bytes = prev_start;
            }
            edit_mode_render(prefix, new_name, cursor_x, cursor_y, cursor_bytes);
            continue;
        }
        else {
            // read one UTF-8 codepoint (may be multiple bytes)
            unsigned char uc = (unsigned char)c;
            char mb[MB_CUR_MAX];
            size_t mblen = 0;
            mb[mblen++] = (char)uc;
            int expected = 1;
            if(uc >= 0xC0){
                if((uc & 0xE0) == 0xC0) expected = 2;
                else if((uc & 0xF0) == 0xE0) expected = 3;
                else if((uc & 0xF8) == 0xF0) expected = 4;
            }
            while(mblen < (size_t)expected){
                int c2 = next_char();
                if(c2 == EOF) break;
                mb[mblen++] = (char)c2;
            }
            int cur_len = (int)strlen(new_name);
            if(cur_len + (int)mblen < MAX_WIDTH){
                memmove(new_name + cursor_bytes + mblen, new_name + cursor_bytes, strlen(new_name + cursor_bytes) + 1);
                memcpy(new_name + cursor_bytes, mb, mblen);
                cursor_bytes += mblen;
                edit_mode_render(prefix, new_name, cursor_x, cursor_y, cursor_bytes);
            }else{
                log_info("edit_mode: input exceeded max length %d\n", MAX_WIDTH);
                ui_show_message(ctx, "Input exceeded max length %d", MAX_WIDTH);
                continue;
            }
        }

        // redraw the whole line to handle wide chars correctly
        fflush(stdout);
    }

    hide_cursor();

    tty_mode_restore(tty_mode_saved);
    return strdup(new_name);
}

char* ui_get_name(void *ui_ctx, char *terminated_character){
    UiContext *ctx = (UiContext *)ui_ctx;
    const char *initial_text = "";
    if(ctx != NULL && ctx->app != NULL && ctx->app->node_text != NULL){
        initial_text = ctx->app->node_text;
    }

    // edit in node position
    char *name = edit_mode(
        ctx,
        "",
        initial_text,
        strlen(initial_text),
        ctx->current_text_x,
        ctx->current_text_y,
        terminated_character
    );
    log_debug("ui_get_name: got name '%s'\n", name);
    return name;
}

char* ui_get_name_append(void *ui_ctx, const char *old_name, char *terminated_character){
    UiContext *ctx = (UiContext *)ui_ctx;
    char *name = edit_mode(
        ctx,
        "",
        old_name,
        strlen(old_name),
        ctx->current_text_x,
        ctx->current_text_y,
        terminated_character
    );
    log_debug("ui_get_name_append: got name '%s'\n", name);
    return name;
}

static char* ui_get_name_prepend(void *ui_ctx, const char *old_name, char *terminated_character){
    UiContext *ctx = (UiContext *)ui_ctx;
    char *name = edit_mode(
        ctx,
        "",
        old_name,
        0,
        ctx->current_text_x,
        ctx->current_text_y,
        terminated_character
    );
    log_debug("ui_get_name_prepend: got name '%s'\n", name);
    return name;
}

char* ui_get_command(void *ui_ctx){
    UiContext *ctx = (UiContext *)ui_ctx;
    // command mode uses fixed position (bottom of screen)
    int cursor_x = 0;
    int cursor_y = ctx ? ctx->height - 1 : 0;
    char terminated_character = 0;
    char *command = edit_mode(ctx, ":", "", 0, cursor_x, cursor_y, &terminated_character);
    log_debug("ui_get_command: got command '%s'\n", command);
    return command;
}

// char* ui_get_name(void *ui_get_name_ctx, char *terminated_character){
char* ui_get_search_query(void *ui_ctx){
    UiContext *ctx = (UiContext *)ui_ctx;
    // search mode uses fixed position (bottom of screen)
    int cursor_x = 0;
    int cursor_y = ctx ? ctx->height - 1 : 0;
    char terminated_character = 0;
    char *query = edit_mode(ctx, "/", "", 0, cursor_x, cursor_y, &terminated_character);
    log_debug("ui_get_search_query: got query '%s'\n", query);
    return query;
}

char *ui_get_search_backward_query(void *ui_ctx){
    UiContext *ctx = (UiContext *)ui_ctx;
    // reverse search mode uses fixed position (bottom of screen)
    int cursor_x = 0;
    int cursor_y = ctx ? ctx->height - 1 : 0;
    char terminated_character = 0;
    char *query = edit_mode(ctx, "?", "", 0, cursor_x, cursor_y, &terminated_character);
    log_debug("ui_get_search_backward_query: got query '%s'\n", query);
    return query;
}

void ui_set_get_name_callback(UiContext *ctx, UiGetNameFn fn){
    if(ctx){
        ctx->get_name_fn = fn;
    }
}

void ui_set_overlay(UiContext *ctx, TreeOverlay *overlay){
    if(ctx){
        ctx->overlay = overlay;
    }
}

UiContext* ui_context_create(int width, int height){
    system("stty -icrnl");// distinguish between Enter and Ctrl+J
#ifdef __APPLE__
    /*
     * macOS / BSD:
     * ^O is bound to VDISCARD (stty: discard)
     * Undefine it so ^O can reach user space.
     */
    system("stty discard undef");
    system("stty dsusp undef"); // ^Y is bound to VDSUSP (stty: susp)
#endif
    UiContext *ctx = (UiContext*)calloc(1, sizeof(UiContext));
    if (ctx == NULL) {
        log_error("ui_context_create: failed to allocate UiContext");
        return NULL;
    }
    ctx->magic = UI_CONTEXT_MAGIC;
    ctx->width = width;
    ctx->height = height;
    ctx->offset_x = 0;
    ctx->offset_y = 0;
    ctx->view_x = 0;
    ctx->view_y = 0;
    ctx->app = NULL;  // set later
    ctx->overlay = NULL;  // set later
    ctx->get_name_fn = NULL; // use default get_name if not set

    return ctx;
}

void ui_context_destroy(UiContext *ctx){
    if(ctx) free(ctx);
}

// ========================= Helper Functions =========================

// Get node text or empty string if node is null
static inline const char* get_tree_node_text(TreeNode n) {
    if (tree_node_is_null(n)) return "";
    return tree_node_text(n);
}

static void ui_render_node(UiContext *ctx, TreeOverlay *ov, TreeNode node, int depth, int *max_lines){
    if(tree_node_is_null(node) || *max_lines <= 0 || 
    (tree_node_hidden(node) && ctx->app->global_enable_hide)
){
        return;
    }
    static char render_node_buf[1024];
    int off = snprintf(render_node_buf, sizeof(render_node_buf), "\n");
    for(int i=0; i<depth; i++) {
        off += snprintf(render_node_buf + off, sizeof(render_node_buf) - off, "\t");
    }
    off += snprintf(render_node_buf + off, sizeof(render_node_buf) - off, "#%llu %s", tree_node_id(node), get_tree_node_text(node));
    bool collapsed = tree_node_is_collapsed(node);
    if(collapsed){
        off += snprintf(render_node_buf + off, sizeof(render_node_buf) - off, " [collapsed]");
    }
    AppState *app = ctx->app;
    if(tree_node_id(app->current_node) == tree_node_id(node)){
        // highlight current node
        printf("\033[7m%s\033[0m", render_node_buf);
    } else {
        printf("%s", render_node_buf);
    }

    *max_lines -= 1;
    if(!collapsed) {
        // render children
        TreeNode child = tree_node_first_child(ov, node);
        while(!tree_node_is_null(child)){
            ui_render_node(ctx, ov, child, depth + 1, max_lines);
            child = tree_node_next_sibling(ov, child);
        }
    }
}

void ui_render_status_bar(UiContext *ctx){
    AppState *app = ctx->app;
    // move cursor to bottom line
    printf("\033[%d;1H", ctx->height);
    // clear line
    printf("\033[2K");
    if(ctx->show_ancestors_in_one_line) {
        printf("ID: %llu, height: %llu, descendents: %llu, name: %.32s, %.82s", 
            tree_node_id(app->current_node),
            tree_node_layout_height(ctx->overlay, app->current_node),
            tree_node_descendents(app->current_node),
            get_tree_node_text(app->current_node),
            tree_node_show_hidden_children(app->current_node) ? "[show hidden]" : "[hide hidden]"
        );
    }else{
        printf("descendents: %llu", tree_node_descendents(app->current_node));
    }
    if(app->info_message && strlen(app->info_message) > 0){
        printf(" Info: %s", app->info_message);
        app->info_message[0] = '\0'; // clear info message
    }
    // printf("\n");
    fflush(stdout);
}
void ui_render(void *ui_ctx){
    UiContext *ctx = (UiContext *)ui_ctx;
    AppState *app = ctx->app;
    InputState *input_state = app->input_state;
    if(input_state->type == INPUT_STATE_TYPE_SEARCH_BACKWARD_QUERY){
        if(app->search_query != NULL){
            free(app->search_query);
        }
        app->search_query = ui_get_search_backward_query(ui_ctx);
        return;
    }
    if(input_state->type == INPUT_STATE_TYPE_GET_COMMAND){
        if(app->command != NULL){
            free(app->command);
        }   
        app->command = ui_get_command(ui_ctx);
        return;
    }
    if(input_state->type == INPUT_STATE_TYPE_GET_NAME
        || input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
        || input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_END){
        char *previous_node_text = app->node_text;
        char terminated_character = '\0';
        if(input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT){
            const char *current_text = previous_node_text ? previous_node_text : "";
            app->node_text = ui_get_name_prepend(ui_ctx, current_text, &terminated_character);
        }else{
            app->node_text = ui_get_name(ui_ctx, &terminated_character);
        }
        if(previous_node_text != NULL){
            free(previous_node_text);
        }
        if(terminated_character == '\e'){
            // user pressed ESC to cancel renaming
            free(app->node_text);
            app->node_text = NULL;
            log_info("Renaming cancelled by user (ESC pressed)");
            return;
        }else if(terminated_character == '\t'){
            app->input_state->type = INPUT_STATE_TYPE_COMPUTED_INPUT; // treat TAB as add child to tail
            UserOperation *uo = (UserOperation *)malloc(sizeof(UserOperation));
            *uo = (UserOperation){.type = UO_DO_EDIT_NODE};
            queue_enqueue(app->input_state->uo_queue, uo);

            uo = (UserOperation *)malloc(sizeof(UserOperation));
            *uo = (UserOperation){.type = UO_ADD_CHILD_TO_TAIL};
            queue_enqueue(app->input_state->uo_queue, uo);
            return;
        }else{
            app->input_state->type = INPUT_STATE_TYPE_COMPUTED_INPUT; // treat TAB as add child to tail
            UserOperation *uo = (UserOperation *)malloc(sizeof(UserOperation));
            *uo = (UserOperation){.type = UO_DO_EDIT_NODE};
            queue_enqueue(app->input_state->uo_queue, uo);
        }
    }
    if(input_state->type == INPUT_STATE_TYPE_GET_APPEND_TEXT){
        input_state->type = INPUT_STATE_DEFAULT;
        if(app->node_text != NULL){
            free(app->node_text);
            app->node_text = NULL;
        }   
        char terminated_character = '\0';
        app->node_text = ui_get_name_append(ui_ctx, tree_node_text(app->current_node), &terminated_character);
        if(terminated_character == '\e'){
            // user pressed ESC to cancel appending
            free(app->node_text);
            app->node_text = NULL;
            log_info("Appending text cancelled by user (ESC pressed)");
            return;
        }else if(terminated_character == '\t'){
            app->input_state->type = INPUT_STATE_TYPE_COMPUTED_INPUT; // treat TAB as add child to tail
            UserOperation *uo = (UserOperation *)malloc(sizeof(UserOperation));
            *uo = (UserOperation){.type = UO_DO_APPEND_NODE_TEXT};
            queue_enqueue(app->input_state->uo_queue, uo);

            uo = (UserOperation *)malloc(sizeof(UserOperation));
            *uo = (UserOperation){.type = UO_ADD_CHILD_TO_TAIL};
            queue_enqueue(app->input_state->uo_queue, uo);
            return;
        }else{
            app->input_state->type = INPUT_STATE_TYPE_COMPUTED_INPUT; // treat TAB as add child to tail
            UserOperation *uo = (UserOperation *)malloc(sizeof(UserOperation));
            *uo = (UserOperation){.type = UO_DO_APPEND_NODE_TEXT};
            queue_enqueue(app->input_state->uo_queue, uo);
        }
    }

    // make sure current node is visible, no collapsed ancestors
    TreeNode n = app->current_node;
    TreeOverlay *ov = ctx->overlay;
    // get tty size
    ui_adapter_get_terminal_size(&ctx->width, &ctx->height);
    while(!tree_node_is_null(n)){
        TreeNode parent = tree_node_parent(ov, n);
        if(tree_node_is_null(parent)){
            break;
        }
        if(tree_node_is_collapsed(parent)){
            tree_node_set_collapse(ov, &parent, false);
        }
        n = parent;
    }
    // get terminal size
    // get_tty_size(&ctx->width, &ctx->height);
    if(true){
        // use mindmap layout
        ctx->height -= 1; // 1 line for status bar
        mindmap_layout_and_render(ctx); // 1 line for status bar
        ctx->height += 1;
        ui_render_status_bar(ctx);
        return;
    }
    log_debug("[ui] Rendering UI, current node: id=%llu text=%s", tree_node_id(app->current_node), 
        get_tree_node_text(app->current_node));
    // clear screen // move cursor to top-left
    printf("\033[2J");
    printf("\033[H");


    int lines_remaining = ctx->height - 2; // 1 line for status bar
    // ui_render_node(ctx, ov, app->current_node, 0, &lines_remaining);
    ui_render_node(ctx, ov, ctx->overlay->root, 0, &lines_remaining);

    printf("\nID: %llu, name: %s", tree_node_id(app->current_node), get_tree_node_text(app->current_node));
    if(app->info_message && strlen(app->info_message) > 0){
        printf(" | Info: %s", app->info_message);
        app->info_message[0] = '\0'; // clear info message
    }
    printf("\n");
}

/**
 * DFS next node
 * if node has children, return first child
 * else return next sibling
 * if no next sibling, return next sibling of parent
 */
TreeNode ui_dfs_next(TreeOverlay *overlay, TreeNode n) {
    if (tree_node_is_null(n) || !overlay) {
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    
    // return first child if exists
    TreeNode child = tree_node_first_child(overlay, n);
    if (!tree_node_is_null(child)) {
        return child;
    }
    
    // otherwise, find next sibling or ancestor's next sibling
    TreeNode current = n;
    while (true) {
        TreeNode p = tree_node_parent(overlay, current);
        if (tree_node_is_null(p)) break;
        
        TreeNode next_sib = tree_node_next_sibling(overlay, current);
        if (!tree_node_is_null(next_sib)) {
            return next_sib;
        }
        current = p;
    }
    
    // no more nodes to visit
    return (TreeNode){ .kind = TREE_NODE_NULL };
}

/**
 * DFS reverse traversal previous node
 * if node has previous sibling, return its deepest child
 * else return parent
 */
TreeNode ui_dfs_prev(TreeOverlay *overlay, TreeNode n) {
    if (tree_node_is_null(n) || !overlay) {
        log_info("DFS reached beginning of tree");
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }

    TreeNode p = tree_node_parent(overlay, n);
    if (tree_node_is_null(p)) {
        log_info("DFS reached beginning of tree");
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }

    // Find previous sibling
    TreeNode prev_sibling = (TreeNode){ .kind = TREE_NODE_NULL };
    TreeNode curr = tree_node_first_child(overlay, p);
    while (!tree_node_is_null(curr)) {
        // Check if curr is the same as n
        bool is_same = false;
        if (curr.kind == TREE_NODE_MUTABLE && n.kind == TREE_NODE_MUTABLE) {
            is_same = (curr.mut == n.mut);
        } else if (curr.kind == TREE_NODE_DISK && n.kind == TREE_NODE_DISK) {
            is_same = (curr.disk.off == n.disk.off);
        }

        if (is_same) break;
        prev_sibling = curr;
        curr = tree_node_next_sibling(overlay, curr);
    }

    // if previous sibling exists, find its deepest child
    if (!tree_node_is_null(prev_sibling)) {
        TreeNode deepest = prev_sibling;
        while (true) {
            TreeNode child = tree_node_first_child(overlay, deepest);
            if (tree_node_is_null(child)) break;
            // find last child among siblings
            TreeNode last_child = child;
            TreeNode next = tree_node_next_sibling(overlay, child);
            while (!tree_node_is_null(next)) {
                last_child = next;
                next = tree_node_next_sibling(overlay, next);
            }
            deepest = last_child;
        }
        return deepest;
    }

    // otherwise return parent
    return p;
}

void ui_center_view_on_current(void *ctx){
    UiContext *ui = (UiContext *)ctx;
    AppState *app = ui->app;
    int cx = 0, cy = 0, cw = 0, ch = 0;
    mind_node_layout_wh(ui, app->tree_overlay, app->current_node, &cx, &cy, &cw, &ch);
    int current_text_y = cy + ch / 2;

    ui->view_y = current_text_y - ui->height / 2;

}

void ui_place_current_left(void *ui_ctx){
    UiContext *ui = (UiContext *)ui_ctx;
    AppState *app = ui->app;
    int cx = 0, cy = 0, cw = 0, ch = 0;
    mind_node_layout_wh(ui, app->tree_overlay, app->current_node, &cx, &cy, &cw, &ch);

    ui->view_x = cx - 1; // leave some margin
}

void ui_place_current_right(void *ui_ctx){
    UiContext *ui = (UiContext *)ui_ctx;
    AppState *app = ui->app;
    int cx = 0, cy = 0, cw = 0, ch = 0;
    mind_node_layout_wh(ui, app->tree_overlay, app->current_node, &cx, &cy, &cw, &ch);

    ui->view_x = cx + cw - ui->width + 2; // leave some margin
    if(ui->view_x < 0){
        ui->view_x = 0;
    }
}
void ui_view_move(void *ui_ctx, int rows, int cols) {
    UiContext *ui = (UiContext *)ui_ctx;
    ui->view_x += cols;
    if(ui->view_x < 0){
        ui->view_x = 0;
    }
    ui->view_y += rows;
    if(ui->view_y < 0){
        ui->view_y = 0;
    }
    ui->app->fix_view = true;
}

void ui_view_up(void *ui_ctx, int lines){
    UiContext *ui = (UiContext *)ui_ctx;
    ui->view_y -= lines;
    if(ui->view_y < 0){
        ui->view_y = 0;
    }
}

void ui_view_next_page(void *ui_ctx){
    UiContext *ui = (UiContext *)ui_ctx;
    ui->view_y += ui->height;
    ui->app->fix_view = true;
}
void ui_view_prev_page(void *ui_ctx){
    UiContext *ui = (UiContext *)ui_ctx;
    ui->view_y -= ui->height;
    ui->app->fix_view = true;
}
void ui_view_down(void *ui_ctx, int lines){
    UiContext *ui = (UiContext *)ui_ctx;
    ui->view_y += lines;
}


void ui_reset_layout(void *ui_ctx){
    UiContext *ui = (UiContext *)ui_ctx;
    AppState *app = ui->app;
    TreeNode current = app->current_node;
    TreeNode child = ui_first_visible_child(app, current);
    int64_t total_height = 0;
    while(!tree_node_is_null(child)){
        if(tree_node_collapsed(child)){
            total_height += 1;
        } else {
            total_height += tree_node_layout_height(app->tree_overlay, child);
        }
        child = ui_next_visible_sibling(app, child);
    }
    if(total_height == 0){
        total_height = 1;
    }
    tree_node_set_layout_height(app->tree_overlay, &current, total_height);
    app->current_node = current;
}

int mind_node_height(TreeOverlay *ov, TreeNode n) {
    if (tree_node_is_null(n)) {
        return 0;
    }
    if (tree_node_collapsed(n)) {// collapsed n->layout_height stores the non-collapsed height, not 1;
                                // layout layer needs to handle collapsed nodes specially
        return 1;
    }

    return tree_node_layout_height(ov, n);
}

/**
 * 'aa' ... 'az', 'ba' ... 'bz', ... 'za' ... 'zz', 
 * 'aA' ... 'aZ', 'bA' ... 'bZ', ... 'zA' ... 'zZ',
 * 'Aa' ... 'Az', 'Ba' ... 'Bz', ... 'Za' ... 'Zz',
 * 'AA' ... 'AZ', 'BA' ... 'BZ', ... 'ZA' ... 'ZZ'
 * total (26 * 2)^2 = 2704 tags
 */
int ui_tag_index_to_tag(int tag_index, char *tag0, char *tag1){
    if(tag_index < 0 || tag_index >= 2704){
        return -1; // invalid tag index
    }
    if(tag_index < 26 * 26){
        *tag0 = 'a' + tag_index / 26;
        *tag1 = 'a' + tag_index % 26;
    }else if(tag_index < 2 * 26 * 26){
        tag_index -= 26 * 26;
        *tag0 = 'a' + tag_index / 26;
        *tag1 = 'A' + tag_index % 26;
    }else if(tag_index < 3 * 26 * 26){
        tag_index -= 2 * 26 * 26;
        *tag0 = 'A' + tag_index / 26;
        *tag1 = 'a' + tag_index % 26;
    }else{
        tag_index -= 3 * 26 * 26;
        *tag0 = 'A' + tag_index / 26;
        *tag1 = 'A' + tag_index % 26;
    }
    return 0;
}
int ui_tag_to_index(char tag0, char tag1){
    if(tag0 >= 'a' && tag0 <= 'z'){
        if(tag1 >= 'a' && tag1 <= 'z'){
            return (tag0 - 'a') * 26 + (tag1 - 'a');
        }else if(tag1 >= 'A' && tag1 <= 'Z'){
            return 26 * 26 + (tag0 - 'a') * 26 + (tag1 - 'A');
        }
    }else if(tag0 >= 'A' && tag0 <= 'Z'){
        if(tag1 >= 'a' && tag1 <= 'z'){
            return 2 * 26 * 26 + (tag0 - 'A') * 26 + (tag1 - 'a');
        }else if(tag1 >= 'A' && tag1 <= 'Z'){
            return 3 * 26 * 26 + (tag0 - 'A') * 26 + (tag1 - 'A');
        }
    }
    return -1; // invalid tag
}