##To compile
c++ -Wall -Wextra -Werror main.cpp -o mini_db

##To Test
./mini_db port_number .save

##In another terminal
nc localhost port_number | cat -e

