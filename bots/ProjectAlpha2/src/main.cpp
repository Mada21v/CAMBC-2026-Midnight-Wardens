// ProjectAlpha2/src/main.cpp
#include <cambc.h>
#include <iostream>
#include "core/bot.h"
#include "draft.h"

int main() {
    Bot bot; 

    while (true) {
        try {
            if (cambc::is_draft()) {
                Draft::play_turn();
            } else {
                bot.play_turn();
            }
        } catch (const std::exception& error) {
            std::cerr << "bot error: " << error.what() << std::endl;
        } catch (...) {
            std::cerr << "bot error: unknown" << std::endl;
        }

        cambc::end_turn();
    }
}
