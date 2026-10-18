#include <cambc.h>
#include <iostream>
#include "core/bot.h"
#include "draft.h"

int main() {
    while (true) {
        try {
            if (cambc::is_draft()) {
                Draft::play_turn();
            } else {
                // Call the function directly from the Bot namespace
                Bot::play_turn(); 
            }
        } catch (const std::exception& error) {
            std::cout << "bot error: " << error.what() << std::endl;
        } catch (...) {
            std::cout << "bot error: unknown exception" << std::endl;
        }

        // Always yield the turn back to the engine at the end of the loop
        cambc::end_turn();
    }
}