<strong>To compile</strong></br>
c++ -Wall -Wextra -Werror main.cpp -o mini_db</br>

<strong>To Test</strong></br>
./mini_db port_number .save</br>

<strong>In another terminal</strong></br>
nc localhost port_number | cat -e</br>
