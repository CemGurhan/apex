# Apex

Apex is a trade strategy tester. It allows users to test out different trade strategies
against a simulated orderbook via a monte carlo simulation. Apex will track important PnL metrics during 
a strategy run and store them for you to analyze post trade. 

This current iteration of Apex only has one strategy that can be ran against the monte carlo simulation,
but this will be expanded upon in the future. Soon Apex will allow you to supply your own strategies and
will allow you to connect to live exchange data, not just simulated.

# How to Run

From the project root, the following makefile target will allow you to run a given strategy against in a monte carlo simulation:

```
make <strategy_name>
```

The only strategy currently registered is a market making strategy. To run this in a monte carlo simulation, execute this command:

```
make marketmaker
```

After your simulation is finished running, the PnL metrics captured will be stored in the default location `app/sim/pnl`.
Each run will be have metrics stored under a sub directory here with a UUID. Under this run sub directory you will also find
a report.html file that allows you to visualize your PnL metrics.

To configure aspects of your simulation and strategies you can edit the config file found [here](app/sim/simcfg.yaml). Details
on each configuration can be found in the following sections.

# Strategies

This section gives a more in depth explanation on how the existing base strategies work.

## Market Maker Strategy

The market maker strategy is very simple. It tracks inventory during runs and uses our current inventory to deduce where on the order book we place trades. The strategy works iteratively, where 2 trades are placed simultaneously on every iteration. One trade
is placed as a buy order on the bid side of the book and another a sell order on the ask side of the book. The spread between this orders is what we profit off of. The objective of the strategy is to not be directional - we do not want to be long or short at all times. Instead, we want to be reactionary. If one side of the trades fills, we cancel the other side and assess what price to place the next two trades with using our existing inventory and market conditions.

Inventory reflects how much of an asset the strategy currently holds. Negative inventory means the strategy is short (we have sold too much off) and positive inventory means the strategy is long (we have bought too much). 

When the strategy executes trades, it will capture what it deems to be the fair price of the order book by taking the halfway point of the best bid and ask prices. If either bid or ask is missing it will take whichever is present as the fair price. The strategy computes a reservation price by shifting the fair price. Fair price is shifted using the skew_factor and the status of the strategy's inventory with the following formula:

`reservation_price = fair_price - (inventory * skew_factor)`

The base_spread configuration is then used to determine how far from the fair_price to execute buy and sell orders, as follows:

`bid_price = reservation_price - base_spread`

`ask_price = reservation_price + base_spread`

The strategy will then place buy and a sell orders at these prices. Note how the reservation_price formula aids in the strategy's goal of being reactionary. If our inventory was negative it means we're too short and instead want to buy more to pocket the spread. A negative inventory therefore means we move up from the fair price with a multiple of skew_factor. A higher reservation_price means a greater likelihood of the buy side of our strategy filling and a lower chance of the sell side filling (as we buy higher but also sell higher). This will aid us in being less short and allow us to pocket some spread. The opposite holds true if we were too long.

Every time the strategy gets a fill from one of its sides, it updates its inventory, cancels the other side's order and repeats
the computation of reservation price + order placement cycle explained before. If the strategy notices that its reached the
max_inventory defined in its config it won't place an order on a given side. E.g. if the max inventory was 20 and our inventory
is now at -20, we're too short, so on the next iteration of the strategy we only place a buy order.

The strategy has its shortcomings. It currently doesn't track the prices it executes trades at, so there is a small chance that the fair price of the book can be a result of the last orders placed by the strategy on the book. This is unlikely to happen given
the simulated orderbook is quite liquid, but for an illiquid market this could present a problem.

The strategy is also susceptible to adverse selection. If an informed trader is aware of the price going up they might buy from
our sell side order. Our strategy will then be short and will move upwards with the market. That trader can now sell higher and make a profit and then repeat the process. Our sell side will be repeatedly hit and we'll go shorter and shorter with no profit.
The simulated order book is designed to simulate adverse selection too, so the strategy must be tweaked if you want to remain
profitable during simulations. E.g. widening your base spread or increasing your skew factor can guard against adverse selection
by ensuring your trades a further away from prices targeted by well informed traders. This will come at the cost of fill rate.
