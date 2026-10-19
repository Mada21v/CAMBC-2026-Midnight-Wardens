// ProjectAlpha2/src/main.cpp
#include <cambc.h>
#include <iostream>
#include "core/bot.h"
#include "draft.h"

int main() {
    // Instantiate the per-champion state
    Bot bot; 

    while (true) {
        try {
            // Route to either the draft phase or the gameplay phase
            if (cambc::is_draft()) {
                Draft::play_turn();
            } else {
                bot.play_turn();
            }
        } catch (const std::exception& error) {
            // Log the error but prevent the bot from crashing completely
            std::cout << "bot error: " << error.what() << std::endl;
        } catch (...) {
            std::cout << "bot error: unknown exception" << std::endl;
        }

        // Always yield the turn back to the engine, even if an error occurred
        cambc::end_turn();
    }
}