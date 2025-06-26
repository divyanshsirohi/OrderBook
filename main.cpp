#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>
#include <tuple>
#include <format>
#include <list>
#include <map>
#include<vector>

//------------------------------------------------------------------------------
// Enumerations
//------------------------------------------------------------------------------

// Describes the order's lifetime behavior.
enum class OrderType
{
    GoodTillCancel, // Order remains in book until explicitly cancelled or filled
    FillAndKill     // Order is immediately executed against resting liquidity or cancelled
};

// Indicates whether the order is to buy or sell.
enum class Side
{
    Buy,  // Buying side (bid)
    Sell  // Selling side (ask)
};

//------------------------------------------------------------------------------
// Type Aliases
//------------------------------------------------------------------------------

using Price = std::int32_t;     // Represents price in smallest currency unit (e.g., paise or cents)
using Quantity = std::uint32_t; // Number of shares/contracts/units
using OrderID = std::uint64_t;  // Globally unique identifier for an order

// Represents a price level and aggregated volume at that level.
struct LevelInfo
{
    Price price;        // Price level
    Quantity quantity;  // Cumulative volume at this price
};

// Collection of LevelInfo used to describe one side (bid/ask) of the order book.
using LevelInfos = std::vector<LevelInfo>;

//------------------------------------------------------------------------------
// Orderbook Snapshot Representation
//------------------------------------------------------------------------------

/**
 * @brief Immutable snapshot of bid and ask levels at a point in time.
 *
 * Provides safe read-only access to price ladder data.
 */
class OrderbookLevelInfos {

public:
    /**
     * @brief Constructs a snapshot with given bid and ask levels.
     *
     * @param inputBids Bid side levels (descending sorted)
     * @param inputAsks Ask side levels (ascending sorted)
     */
    OrderbookLevelInfos(const LevelInfos& inputBids, const LevelInfos& inputAsks):
        bids_{ inputBids },
        asks_{ inputAsks}
    { }

    /// @return Const reference to current bid levels.
    const LevelInfos& getBids() const { return bids_; }

    /// @return Const reference to current ask levels.
    const LevelInfos& getAsks() const { return asks_; }

private:
    LevelInfos bids_;  ///< Sorted descending bid levels
    LevelInfos asks_;  ///< Sorted ascending ask levels
};

//------------------------------------------------------------------------------
// Order Definition
//------------------------------------------------------------------------------

/**
 * @brief Represents a single executable order.
 *
 * Tracks order metadata, original size, and remaining quantity.
 */
class Order {
public:
    /**
     * @brief Constructs a new Order instance.
     */
    Order(OrderType ordertype, OrderID orderid, Side side, Quantity quantity, Price price):
        orderType_{ ordertype },
        orderID_{ orderid },
        side_{ side },
        price_{ price },
        initialQuantity_{ quantity },
        remainingQuantity{ quantity }
    {}

    /// @return Unique identifier for the order.
    OrderID getOrderID() const { return orderID_; }

    /// @return Side (Buy or Sell) of the order.
    Side getSide() const { return side_; }

    /// @return Price at which the order is placed.
    Price getPrice() const { return price_; }

    /// @return Type of the order.
    OrderType getOrderType() const { return orderType_; }

    /// @return Original order quantity.
    Quantity getInitialQuantity() const { return initialQuantity_;}

    /// @return Quantity that has not yet been filled.
    Quantity getRemainingQuantity() const { return remainingQuantity; }

    /// @return Quantity that has already been filled.
    Quantity getFilledQuantity() const { return (getInitialQuantity()-getRemainingQuantity());}

    /// @return whether filled or not.
    bool isFilled() const { return getRemainingQuantity() == 0; }

    /**
     * @brief Fills part of the order.
     *
     * @throws std::logic_error If trying to fill more than remaining.
     */
    void Fill(Quantity quantity) {
        if (quantity>getRemainingQuantity()) {
            throw std::logic_error(std::format("order ({}) cannot be filled for more than its remaining quantity", getOrderID()));
        }
        remainingQuantity -= quantity;
    }

private:
    OrderType orderType_;         ///< Order time-in-force behavior
    OrderID orderID_;             ///< Unique ID
    Side side_;                   ///< Buy/Sell
    Price price_;                 ///< Limit price
    Quantity initialQuantity_;    ///< Original order size
    Quantity remainingQuantity;   ///< Unfilled portion
};

// Shared ownership of orders for safe memory tracking across containers.
using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;  // Maintains ordering of orders at a price level

//------------------------------------------------------------------------------
// Order Modification Request
//------------------------------------------------------------------------------

/**
 * @brief Represents a request to modify or insert an order.
 */
class OrderModify {
public:
    OrderModify(OrderID orderid, Side side, Quantity quantity, Price price):
        orderid_{ orderid },
        side_{ side },
        quantity_{ quantity },
        price_{ price }
    { }

    /// @return ID of the order being modified.
    OrderID getOrderID() const { return orderid_; }

    /// @return New limit price.
    Price getPrice() const { return price_; }

    /// @return New side of the order.
    Side getSide() const { return side_; }

    /// @return New quantity.
    Quantity getQuantity() const { return quantity_; }

    /**
     * @brief Converts to a fully-formed Order instance with specified type.
     *
     * @param type Time-in-force (GTC/FAK)
     * @return OrderPointer New shared order object.
     */
    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, getOrderID(), getSide(), getQuantity(), getPrice());
    }

private:
    OrderID orderid_;   ///< ID of the order to be modified
    Side side_;         ///< New direction (buy/sell)
    Price price_;       ///< Updated price
    Quantity quantity_; ///< Updated size
};

//------------------------------------------------------------------------------
// Trade Info & Execution Representation
//------------------------------------------------------------------------------

/**
 * @brief Trade execution metadata for one side of a transaction.
 */
struct TradeInfo {
    OrderID orderid_;     ///< Order involved in the trade
    Price price_;       ///< Execution price
    Quantity quantity_; ///< Execution size
};

/**
 * @brief Represents a completed trade between a buy and a sell order.
 */
class Trade {
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_{ bidTrade },
          askTrade_{ askTrade }
    { }

    /// @return Trade information for the buyer.
    const TradeInfo& getBidTrade() const { return bidTrade_; }

    /// @return Trade information for the seller.
    const TradeInfo& getAskTrade() const { return askTrade_; }

private:
    TradeInfo bidTrade_; ///< Buyer side details
    TradeInfo askTrade_; ///< Seller side details
};

using Trades = std::vector<Trade>; // Collection of trade events

//------------------------------------------------------------------------------
// Order Book
//------------------------------------------------------------------------------

/**
 * @brief Core order book implementation.
 *
 * Maintains price-time-priority queues for both sides of the market,
 * along with mapping from OrderID to internal state.
 */
class OrderBook {
public:

    /**
     * @brief Adds a new order to the order book and attempts to match it.
     *
     * Enforces unique order IDs, and FillAndKill logic. If matching is possible,
     * triggers the match engine to execute trades.
     *
     * @param order Shared pointer to the incoming order.
     * @return Trades executed as a result of this order (can be empty).
     */
    Trades AddOrder(OrderPointer order) {
        // Reject duplicate orders (same OrderID already exists in the book)
        if (orders_.contains(order->getOrderID()))
            return { };

        // FillAndKill order cannot be added if it cannot immediately match
        if (order->getOrderType() == OrderType::FillAndKill && !CanMatch(order->getSide(), order->getPrice()))
            return { };

        OrderPointers::iterator iterator;

        // Insert order into bid or ask book, maintain price-time priority
        if (order->getSide() == Side::Buy) {
            auto& orders = bids_[order->getPrice()]; // Get or create bid level
            orders.push_back(order);                 // Append to FIFO queue
            iterator = std::next(orders.begin(),orders.size()-1); // Track position
        }
        else {
            auto& orders = asks_[order->getPrice()]; // Get or create ask level
            orders.push_back(order);                 // Append to FIFO queue
            iterator = std::next(orders.begin(),orders.size()-1); // Track position
        }

        // Register order in global lookup table with position info
        orders_.insert({order->getOrderID(),OrderEntry{order, iterator } });

        // Attempt to match with opposing orders
        return MatchOrders();
    }

private:

    // Internal structure linking order pointer and its location in the book
    struct OrderEntry {
        OrderPointer order_{nullptr};              // Actual order object
        OrderPointers::iterator location_;         // Iterator in the price level list
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_; // Bid side (sorted descending)
    std::map<Price, OrderPointers, std::less<Price>> asks_;    // Ask side (sorted ascending)
    std::unordered_map<OrderID, OrderEntry> orders_;           // Fast global lookup

    /**
     * @brief Determines if an incoming order can be matched against the current order book.
     *
     * For a buy order: checks if there's at least one ask priced <= buy price.
     * For a sell order: checks if there's at least one bid priced >= sell price.
     *
     * This is typically used as a precondition before entering the match loop.
     *
     * @param side  Side of the incoming order (Buy or Sell).
     * @param price Limit price of the incoming order.
     * @return true  If the order can match immediately with an opposing book level.
     * @return false If there is no valid counterparty at the specified price.
     */
    bool CanMatch(Side side, Price price) const {
        if (side == Side::Buy) {
            if (asks_.empty())
                return false;

            const auto& [bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        }
        else {
            if (bids_.empty())
                return false;

            const auto& [bestBid, _] = *bids_.begin();
            return price <= bestBid;
        }
    }

    /**
     * @brief Matches opposing orders in the book based on price-time priority.
     *
     * Continuously matches top-of-book bid and ask orders while prices cross.
     * Each match executes at the resting order's price. Handles full and partial fills,
     * and removes fully filled orders from both the book and the global map.
     * Also cancels unmatched FillAndKill orders after the matching loop.
     *
     * @return A list of trades executed during the matching process.
     */
    Trades MatchOrders() {
        Trades trades;
        trades.reserve(orders_.size()); // Reserve space to avoid reallocation

        while (true) {
            // Terminate if one side of the book is empty
            if (bids_.empty() || asks_.empty())
                break;

            auto& [bidPrice, bids] = *bids_.begin(); // Best bid level
            auto& [askPrice, asks] = *asks_.begin(); // Best ask level

            // No price crossing — matching ends
            if (bidPrice < askPrice)
                break;

            // Match top-of-book orders while levels are non-empty
            while (!bids.empty() && !asks.empty()) {
                auto& bid = bids.front(); // Oldest buy order at best bid
                auto& ask = asks.front(); // Oldest sell order at best ask

                Quantity quantity = std::min(bid->getRemainingQuantity(), ask->getRemainingQuantity());

                // Execute trade: reduce quantity on both sides
                bid->Fill(quantity);
                ask->Fill(quantity);

                // Remove fully filled orders from book + registry
                if (bid->isFilled()) {
                    bids.pop_front();
                    orders_.erase(bid->getOrderID());
                }
                if (ask->isFilled()) {
                    asks.pop_front();
                    orders_.erase(ask->getOrderID());
                }

                // Clean up empty price levels
                if (bids.empty())
                    bids_.erase(bidPrice);
                if (asks.empty())
                    asks_.erase(askPrice);

                // Log the trade execution
                trades.push_back(Trade{
                    TradeInfo{bid->getOrderID(), bid->getPrice(), quantity},
                    TradeInfo{ask->getOrderID(), ask->getPrice(), quantity}
                });
            }
        }

        // Cancel unmatched FAK (FillAndKill) orders that did not find a match
        if (!bids_.empty()) {
            auto& [_, bids] = *bids_.begin();
            auto& order = bids.front();
            if (order->getOrderType() == OrderType::FillAndKill)
                CancelOrder(order->getOrderID());
        }

        if (!asks_.empty()) {
            auto& [_, asks] = *asks_.begin();
            auto& order = asks.front();
            if (order->getOrderType() == OrderType::FillAndKill)
                CancelOrder(order->getOrderID());
        }

        return trades;
    }
};

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

int main() {
    return 0;
}
