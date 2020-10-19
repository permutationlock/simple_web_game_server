#ifndef CATACRAWL_PLAYER_HPP
#define CATACRAWL_PLAYER_HPP

typedef unsigned int player_id;

class player {
    public:
        player(player_id i, websocketpp::connection_hdl c);

        websocketpp::connection_hdl get_connection();
        void set_connection(websocketpp::connection_hdl c);

        double get_x();
        void set_x(double nx);
        double get_y();
        void set_y(double ny);

        player_id get_id();
    private:
        player_id id;
        websocketpp::connection_hdl connection;
        double x, y;
};


player::player(player_id i, websocketpp::connection_hdl c) :
    id(i), connection(c) {}

websocketpp::connection_hdl player::get_connection() {
    return connection;
}

void player::set_connection(websocketpp::connection_hdl c) {
    connection = c;
}

double player::get_x() {
    return x;
}

void player::set_x(double nx) {
    x = nx;
}

double player::get_y() {
    return y;
}

void player::set_y(double ny) {
    y = ny;
}

player_id player::get_id() {
    return id;
}

#endif // CATACRAWL_PLAYER_HPP
