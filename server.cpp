#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_set>
#include <vector>
#include <fstream>

#define PORT 8080

int counter;
std::unordered_set<std::string> validWords;
std::vector<std::string> answerList;

// converts strings to lowercase and removes non-apha characters
std::string formatWord(std::string& str) {
    std::string result;
    for (auto c : str)
        if (std::isalpha(c))
            result.push_back(std::tolower(c));
    return result;
}

// loads a set of valid words from a specified file
std::unordered_set<std::string> loadValidWords(std::string filename) {

    // open file
    std::ifstream inputFile(filename);
    if( !inputFile ) {
        std::cerr << "Error: Could not open file" << std::endl;
        exit(1);
    }

    // parse file
    std::unordered_set<std::string> valid_words;
    std::string word;
    while (inputFile >> word) {
        if (word.length() == 5) {
            valid_words.insert(formatWord(word));
        } else {
            std::cout << "Warning: " << word << " is not the right length" << std::endl;
        }
    }

    return valid_words;

}

std::vector<std::string> loadAnswerList(std::string filename, std::unordered_set<std::string> validWords) {
    
    std::ifstream inputFile(filename);
    if (!inputFile) {
        std::cerr << "Error: Could not open file" << std::endl;
        exit(1);
    }

    std::vector<std::string> answerList;
    std::string word;
    while (inputFile >> word) {
        if ( validWords.find(word) != validWords.end() ) {
            answerList.push_back(word);
        } else {
            std::cout << "Warning: " << word << " not a valid word" << std::endl;
        }
    }

    return answerList;

}

// Generates wordle responce given guess and answer
std::string generateHint(std::string& guess, std::string& answer) {
    std::string hint;

    for ( int i = 0; i < 5; ++i) {
        // Check if letter in the correct spot
        if ( guess[i] == answer[i] )
            hint.push_back(std::toupper(guess[i]));
        // check if letter exist in an incorrect spot
        else {
            bool partial_match = false;
            for (int j = 0; j < 5; ++j) {
                if (j != i && guess[i] == answer[j] && guess[j] != answer[j]) {
                    partial_match = true;
                    break;
                }
            }
            hint.push_back(partial_match ? guess[i] : '*');
        }
    }
    return hint;
}

// Handles the Wordle game logic for a connected client
void playWordle(int client_fd) {
    char buffer[1024];

    for (auto answer : answerList) {
        int attempts_left = 6;
        
        while (true) {
            std::string message = "Please enter 5-letter word (" + std::to_string(attempts_left) + " attempts remaining): ";
            send(client_fd, message.c_str(), message.length(), 0);
            

            // Read the guess from the client
            if (read(client_fd, buffer, 1024) < 0) {
                std::cout << "Warning read() failed" << std::endl;
                continue;
            }
            std::string guess = buffer;
            guess = formatWord(guess);

            // Check if the guess is a valid word
            if (guess.length() != 5) {
                const char* m = "Invalid input! Please enter exactly 5 letters.\n";
                int n = guess.length();
                send(client_fd, &n, sizeof(n), 0);
                continue;
            }
            if ( validWords.find(guess) == validWords.end() ) {
                const char* m = "Word not in word list. Try again\n";
                send(client_fd, m, strlen(m), 0);
                continue;
            }

            // Send hint to the client
            std::string hint = generateHint(guess, answer) + "\n";

            send(client_fd, hint.c_str(), hint.length(), 0);

            // Check for win condition
            if (guess == answer) {
                std::string m = "You Won!\n";
                send(client_fd, m.c_str(), m.length(), 0);
                break;
            }
            
            --attempts_left; 
            if (!attempts_left) {
                std::string m = "Game Over!\nThe correct word was " + answer + "\n";
                send(client_fd, m.c_str(), m.length(), 0);
                break;
            }
        }

        // Promt client to start a new game
        std::string m = "Would you like to continue? (y/n) ";
        send(client_fd, m.c_str(), m.length(), 0);

        while (true) {
            if (read(client_fd, buffer, 1024) < 0) {
                std::cout << "Warning read() failed" << std::endl;
                continue;
            }
            if (std::tolower(buffer[0]) == 'y') {
                std::string m = "Starting a new game\n";
                send(client_fd, m.c_str(), m.length(), 0);
                break;
            }
            else if (std::tolower(buffer[0]) == 'n') {
                close(client_fd);
                return;
            }
        }
    }

    std::string m = "Congratulations! You have solved all the wordle games\n";
    send(client_fd, m.c_str(), m.length(), 0);   

    close(client_fd);
}

void error(const std::string& msg) {
    std::cerr << msg << std::endl;
    exit(1);
}

int main(int argc, char** argv) {

    /* Handle command line arguments */
    if (argc != 3)
        error("Usage: <executable> <wordlist_file> <answer_file>");

    std::string wordListFilename = argv[1];
    std::string answerListFilename = argv[2];

    // Load set of valid words from given file
    validWords = loadValidWords(wordListFilename);
    answerList = loadAnswerList(answerListFilename, validWords);
    

    // Set up TCP connection for server
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        error("Socket creation failed");

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        error("Bind failed");
    
    if (listen(server_fd, 3) < 0)
        error("Listen failed");

    std::cout << "Server listening on port " << PORT << std::endl;

    // Main server loop
    counter = 0;
    while (true) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0)
            error("Accept failed");

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd); // Child process: handle the client
            std::cout << "Starting child process " << counter << std::endl;
            playWordle(client_fd);
            std::cout << "Ending child process " << counter << std::endl;
            exit(0); // End the child process
        }
        else if (pid > 0) {
            close(client_fd); // Parent process: continue accepting new clients
        }
        ++counter;

    }

    close(server_fd);
    return 0;
}
