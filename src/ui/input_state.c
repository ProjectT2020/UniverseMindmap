#include <stdlib.h>
#include <string.h>

#include "input_state.h"
#include "ui.h"


InputState* input_state_create(){
    InputState *state = (InputState*)calloc(1, sizeof(InputState));
    if(state){
        state->type = INPUT_STATE_DEFAULT;
        state->prefix = '\0';
        state->uo_queue = queue_create(8);
    }
    return state;
}

UserOperation input_convert(InputState *input_state, char key, unsigned short keyCode,
    const char *text, bool isControlDown){
  if (input_state->type == INPUT_STATE_TYPE_SEARCH_QUERY) {
    input_state->type = INPUT_STATE_DEFAULT;
    return (UserOperation){.type = UO_DO_SEARCH, .data = (void*)strdup(text)};
  }
  if(input_state->type == INPUT_STATE_TYPE_JUMP_TO_VISIBLE_TAG){
     if(input_state->prefix_count == 0){
          input_state->prefix_count++;
          input_state->key_buffer[0] = key;
          return (UserOperation){.type = UO_NOP};
      } else {
          input_state->type = INPUT_STATE_DEFAULT;
          input_state->prefix_count = 0;
          input_state->key_buffer[1] = key;
          UserOperation uo;
          uo.type = UO_JUMP_TO_UI_NODE_MARK;
          uo.param1 = input_state->key_buffer[0];
          uo.param2 = input_state->key_buffer[1];
          return uo;
     }
  }
  if (input_state->type == INPUT_STATE_PREFIX) {
    switch (input_state->prefix) {
        case '\'':
            input_state->type = INPUT_STATE_DEFAULT;
            if(('a' <= key && key <= 'z') || ('A' <= key && key <= 'Z') || ('0' <= key && key <= '9')){
                return (UserOperation){.type = UO_JUMP_TO_MARK, .param1 = key};
            } else {
                return (UserOperation){.type = UO_NOP};
            }
       case '\\':
          {
            if(input_state->prefix_count == 0){
                input_state->prefix_count++;
                input_state->key_buffer[0] = key;
                return (UserOperation){.type = UO_NOP};
            } else {
                input_state->type = INPUT_STATE_DEFAULT;
                input_state->prefix_count = 0;
                input_state->key_buffer[1] = key;
                if(strcmp(input_state->key_buffer, "ac") == 0){// as current
                    return (UserOperation){.type = UO_AS_CURRENT_TASK};
                } 
                if(strcmp(input_state->key_buffer, "ft") == 0){// finish task
                    return (UserOperation){.type = UO_FINISH_TASK};
                }
                if(strcmp(input_state->key_buffer, "cd") == 0){// current definition
                    return (UserOperation){.type = UO_CURRENT_TASK_JUMP_DEFINITION};
                }
                if(strcmp(input_state->key_buffer, "gs") == 0){// go web search
                    return (UserOperation){.type = UO_SEARCH_ENGINE};
                }
                return (UserOperation){.type = UO_NOP};
            }
          }
    case 'd':
          switch(key){
              case 'd':
                input_state->type = INPUT_STATE_DEFAULT;
                return (UserOperation){.type = UO_CUT_SUBTREE};
              default:
                input_state->type = INPUT_STATE_DEFAULT;
                return (UserOperation){.type = UO_NOP};
          }
    case 'g': // prefix g
      switch (key) {
      case 'g':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_MOVE_FOCUS_TOP};
      case 'c':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_MOVE_FOCUS_CURRENT_TASK};
      case 'd':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_JUMP_KEYWORD_DEFINITION};
      case 'D':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_OPEN_RESOURCE_LINK};
      case 'S':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_SEARCH_ENGINE};
      case 'j':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_MOVE_FOCUS_DOWN};
      case 'k':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_MOVE_FOCUS_UP};
      case ';':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_TO_EDIT_HISTORY};
      case 'y':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_COPY_TEXT_TO_SYSTEM_CLIPBOARD};
      case 'Y':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_COPY_SUBTREE_TO_SYSTEM_CLIPBOARD};
      case 'p':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_PASTE_AS_CHILD};
      default:
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_NOP};
      }
    case 'm':
      switch (key) {
      case '[':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_MARK_AS_DEFINITION};
      case ']':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_UNMARK_AS_DEFINITION};
      default:
        if(('a' <= key && key <= 'z') || ('A' <= key && key <= 'Z') || ('0' <= key && key <= '9')){
            input_state->type = INPUT_STATE_DEFAULT;
            return (UserOperation){.type = UO_MARK_NODE, .param1 = key};
        } else {
            input_state->type = INPUT_STATE_DEFAULT;
            return (UserOperation){.type = UO_NOP};
        }
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_NOP};
      }
      break;
    case '[':{
      input_state->type = INPUT_STATE_DEFAULT;
      switch (key) {
      case '[':
        return (UserOperation){.type = UO_MOVE_PARENT_PREV_SIBLING_BEGIN};
      case ']':
        return (UserOperation){.type = UO_MOVE_PARENT_PREV_SIBLING_END};
      case 't':
        return (UserOperation){.type = UO_PREV_TASK};
      default:
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_NOP};
      }
      break;
    }
    case ']':{
            input_state->type = INPUT_STATE_DEFAULT;
            switch(key){
                case ']':
                  return (UserOperation){.type = UO_MOVE_PARENT_NEXT_SIBLING_END};
                case '[':
                  return (UserOperation){.type = UO_MOVE_PARENT_NEXT_SIBLING_BEGIN};
                case 't':
                  return (UserOperation){.type = UO_NEXT_TASK};
                default:
                  return (UserOperation){.type = UO_NOP};
            }
    }
    case 'Z':{
      switch (key) {
      case 'Z':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_EXIT_SAVE};
      default:
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_NOP};
      }
      break;
    }
    case 'z':
      switch (key) {
      case 'c':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_FOLD_NODE};
      case 'o':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_UNFOLD_NODE};
      case 'r':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_REDUCE_FOLDING};
      case 'R':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_EXPAND_ALL_DESCENDANTS};
      case 'm':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_FOLD_MORE};
      case 'M':
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_FOLD_LEVEL_1};
       case '.':
       case 'z':
         input_state->type = INPUT_STATE_DEFAULT;
         return (UserOperation){.type = UO_CENTER_VIEW};
           case 's':
               input_state->type = INPUT_STATE_DEFAULT;
               return (UserOperation){.type = UO_PLACE_LEFT};
          case 'e':
              input_state->type = INPUT_STATE_DEFAULT;
              return (UserOperation){.type = UO_PLACE_RIGHT};
          case 'H':
              input_state->type = INPUT_STATE_DEFAULT;
              return (UserOperation){.type = UO_VIEW_HALF_SCREEN_LEFT};
          case 'L':
              input_state->type = INPUT_STATE_DEFAULT;
              return (UserOperation){.type = UO_VIEW_HALF_SCREEN_RIGHT};
      default:
        input_state->type = INPUT_STATE_DEFAULT;
        return (UserOperation){.type = UO_NOP};
      }
    default:
      input_state->type = INPUT_STATE_DEFAULT;
      return (UserOperation){.type = UO_NOP};
    }
  }
    if(isControlDown){
      switch(key){
        case 'd':
          return (UserOperation){.type = UO_NEXT_HALF_PAGE};
        case 'u':
          return (UserOperation){.type = UO_PREV_HALF_PAGE};
        case 'f':
          return (UserOperation){.type = UO_NEXT_PAGE};
        case 'b':
          return (UserOperation){.type = UO_PREV_PAGE};
        case 'o':
          return (UserOperation){.type = UO_JUMP_BACK};
        case 'i':
          return (UserOperation){.type = UO_JUMP_FORWARD};
        default:
          return (UserOperation){.type = UO_NOP};
      }
          return (UserOperation){.type = UO_NOP};
    }

    // Normal key handling
    switch(key){
        case 'm':
        case '\'':
        case 'd':
        case 'g':
        case 'z':
        case 'Z':
        case '[':
        case ']':
            input_state->type = INPUT_STATE_PREFIX;
            input_state->prefix = key;
            return (UserOperation){.type = UO_NOP};
        case '\\':
            input_state->type = INPUT_STATE_PREFIX;
            input_state->prefix = key;
            input_state->prefix_count = 0;
            return (UserOperation){.type = UO_NOP};
        case 'D':
            return (UserOperation){.type = UO_DELETE_SUBTREE};
        case 'J':
            return (UserOperation){.type = UO_JOIN_SIBLING_AS_CHILD};
        case 'G':
            return (UserOperation){.type = UO_MOVE_FOCUS_BOTTOM};
        case 'h':
            return (UserOperation){.type = UO_MOVE_FOCUS_LEFT};
        case 'j':
            return (UserOperation){.type = UO_MOVE_FOCUS_NEXT_SIBLING};
        case 'k':
            return (UserOperation){.type = UO_MOVE_FOCUS_PREV_SIBLING};
        case 'l':
            return (UserOperation){.type = UO_MOVE_FOCUS_RIGHT};
        case 'e':
            return (UserOperation){.type = UO_MOVE_FOCUS_LAST_CHILD};
        case 'y':
            return (UserOperation){.type = UO_COPY_SUBTREE};
        case 'p':
            return (UserOperation){.type = UO_PASTE_SIBLING_BELOW};
        case 'P':
            return (UserOperation){.type = UO_PASTE_SIBLING_ABOVE};
        case 't':
            input_state->type = INPUT_STATE_TYPE_JUMP_TO_VISIBLE_TAG;
            input_state->prefix = 't';
            return (UserOperation){.type = UO_PREPARE_JUMP_TO_VISIBLE_TAG};
        case 'n':
            return (UserOperation){.type = UO_SEARCH_NEXT};
        case 'N':
            return (UserOperation){.type = UO_SEARCH_PREV};
        case 'a':
            return (UserOperation){.type = UO_EDIT_NODE_END};
        case 'i':
            return (UserOperation){.type = UO_EDIT_NODE_FRONT};
        case 's':
            return (UserOperation){.type = UO_EDIT_NODE};
        case '\t':
        case 'A':
            return (UserOperation){.type = UO_ADD_CHILD_TO_TAIL};
        case 'x':
            return (UserOperation){.type = UO_CUT_NODE};
        case '\n':
        case '\r':
        case 'o':
            return (UserOperation){.type = UO_ADD_SIBLING_BELOW};
        case 'O':
            return (UserOperation){.type = UO_ADD_SIBLING_ABOVE};
        case '#':
            return (UserOperation){.type = UO_SEARCH_PREV_EXACT};
        case '*':
             return (UserOperation){.type = UO_SEARCH_NEXT_EXACT};
        case '/':
            return (UserOperation){.type = UO_SEARCH};
        case '^':
            return (UserOperation){.type = UO_MOVE_FOCUS_TERM_ROOT};
        case '0':
            return (UserOperation){.type = UO_MOVE_FOCUS_HOME};
        case ' ':
            input_state->type = INPUT_STATE_DEFAULT;
            return (UserOperation){.type = UO_HIT_SPACE};
        default:
          break;
     }
     switch(keyCode){
        case 123: // ← // left
          return (UserOperation){.type = UO_MOVE_FOCUS_LEFT};
        case 124: // → // right
          return (UserOperation){.type = UO_MOVE_FOCUS_RIGHT};
        case 125: // ↓ // down
          return (UserOperation){.type = UO_MOVE_FOCUS_DOWN};
        case 126: // ↑ // up
          return (UserOperation){.type = UO_MOVE_FOCUS_UP};
    // if(event.keyCode == 121 || event.keyCode == 116 || event.keyCode == 115 || event.keyCode == 119){
        case 116: // page up
          return (UserOperation){.type = UO_PREV_PAGE};
        case 121: // page down
          return (UserOperation){.type = UO_NEXT_PAGE};
        case 115: // home
          return (UserOperation){.type = UO_MOVE_FOCUS_HOME};
        case 117: // delete
          return (UserOperation){.type = UO_CUT_SUBTREE};
        default:
            break;
     }
     return (UserOperation){.type = UO_NOP};
}