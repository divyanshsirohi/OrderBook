#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>
#include <tuple>
#include <format>
#include <list>
#include<vector>

// Enum for type of order: either stays active (GoodTillCancel) or cancels if not filled immediately (FillAndKill)
enum class OrderType
{
    GoodTillCancel,
    FillAndKill
};

// Enum for buy/sell direction of an order
enum class Side
{
    Buy,
    Sell
};

// Type aliases for better readability
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderID = std::uint64_t;

// Struct representing a single level in the orderbook (a price and total quantity at that price)
struct LevelInfo
{
    Price price;
    Quantity quantity;
};

// Alias for a list of LevelInfo entries (i.e., a list of bids or asks)
using LevelInfos = std::vector<LevelInfo>;

// Class holding the current state of the orderbook levels (bids and asks)
class OrderbookLevelInfos {

public:
    // Constructor initializes bids_ and asks_ using initializer list
    OrderbookLevelInfos(const LevelInfos& inputBids, const LevelInfos& inputAsks):
        bids_{ inputBids },
        asks_{ inputAsks}
    { }

    // Getter for bid levels
    const LevelInfos& getBids() const { return bids_; }

    // Getter for ask levels
    const LevelInfos& getAsks() const { return asks_; }

private:
    LevelInfos bids_;  // Internal storage for bid levels
    LevelInfos asks_;  // Internal storage for ask levels
};

// Class representing a single order in the system
class Order {
public:
    // Constructor for Order: sets all properties and initializes remaining quantity
    Order(OrderType ordertype, OrderID orderid, Side side, Quantity quantity, Price price):
        orderType_{ ordertype },
        orderID_{ orderid },
        side_{ side },
        price_{ price },
        initialQuantity_{ quantity },
        remainingQuantity{ quantity }
    {}

    // Returns the order ID
    OrderID getOrderID() const { return orderID_; }

    // Returns whether it's a buy or sell
    Side getSide() const { return side_; }

    // Returns the price of the order
    Price getPrice() const { return price_; }

    // Returns the type of the order (GoodTillCancel or FillAndKill)
    OrderType getOrderType() const { return orderType_; }

    // Returns the original quantity of the order
    Quantity getInitialQuantity() const { return initialQuantity_;}

    // Returns how much of the order is still unfilled
    Quantity getRemainingQuantity() const { return remainingQuantity; }

    // Returns how much of the order has been filled so far
    Quantity getFilledQuantity() const { return (getInitialQuantity()-getRemainingQuantity());}

    // Reduces the remaining quantity by the filled amount, throws error if overfilled
    void Fill(Quantity quantity) {
        if (quantity>getRemainingQuantity()) {
            throw std::logic_error(std::format("order ({}) cannot be filled for more than its remaining quantity", getOrderID()));
        }
        remainingQuantity -= quantity;
    }

private:
    OrderType orderType_;         // Type of the order
    OrderID orderID_;             // Unique order ID
    Side side_;                   // Buy or Sell
    Price price_;                 // Price of the order
    Quantity initialQuantity_;    // Total quantity requested
    Quantity remainingQuantity;   // Quantity yet to be filled
};

// Alias for a shared pointer to an Order object
using OrderPointer= std::shared_ptr<Order>;

// Defines OrderPointers as a list of shared pointers to Order objects
using  OrderPointers = std::list<OrderPointer>;

class OrderModify {
public:
    // Constructor to initialize all member variables using initializer list
    OrderModify(OrderID orderid, Side side, Quantity quantity, Price price):
        orderid_{ orderid },
        side_{ side },
        quantity_{ quantity },
        price_{ price },
    { }

    // Returns the order ID
    OrderID getOrderID() const { return orderid_; }

    // Returns the price
    Price getPrice() const { return price_; }

    // Returns the side (Buy or Sell)
    Side getSide() const { return side_; }

    // Returns the quantity
    Quantity getQuantity() const { return quantity_; }

    // Converts this OrderModify into a shared pointer to a new Order object
    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, getOrderID(), getSide(), getQuantity(), getPrice());
    }

private:
    OrderID orderid_;       // Unique identifier for the order
    Side side_;             // Buy or Sell
    Price price_;           // Price at which the order is to be modified
    Quantity quantity_;     // Quantity to be modified
};

struct TradeInfo {
    Order orderid_;
    Price price_;
    Quantity quantity_;
};

class Trade {
public:
    Trade( const TradeInfo& bidTrade, const TradeInfo& askTrade):
};

int main() {
    return 0;
}
