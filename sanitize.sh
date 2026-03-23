find . utils fastindicator network \( -name "*.cpp" -o -name "*.hpp" \) -print0 | xargs -0 cat | wc -l
g++ -std=c++20 -fsanitize=address -g -O0 -Wall -Wextra main.cpp -o main