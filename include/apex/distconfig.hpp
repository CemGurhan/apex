#pragma once

// DistConfig encapsulates the lambdas, sigma and probabilities used to configure
// the distributions the SimulatedFeeder samples from to drive market activity.
struct DistConfig {
    // sigma controls how volatile our market is, controlling how wide our swings can be
    // when selecting a random price under a normal distribution. Higher sigma means
    // more volatile movements.
    // E.g. value of 2 indicates 2 sigma i.e. ~95% of values fall between 2 and -2,
    // anything out of that range is a 2 sigma event with a probability of 5% of
    // occurring.
    double sigma = 0.01;

    // fair_price_lambda is used to determine how much our order prices cluster towards the fair
    // market price. A higher value indicates limit orders being placed closer to fair price.
    double fair_price_lambda = 10;

    // arrival_rate_lambda is used to determine how frequently orders arrive in our market.
    // A higher value indicates more frequent order arrivals.
    double arrive_rate_lambda = 0.1;

    // order_qty_size_lambda is used to determine how large our order quantities placed are.
    // A larger value means smaller order quantities.
    double order_qty_size_lambda = 10;

    // side_prob is used to determine the ratio of buys and sells
    // via a bernoulli distribution.
    // E.g. 0.5 indicates a 50% chance for either.
    double side_prob = 0.5;

    // aggressive_prob is used to determine how often we place aggressive orders on the opposite
    // side of the book, rather than passive orders on the same side.
    // E.g. 0.1 indicates that 10% of the time, we place an aggressive order.
    double aggressive_prob = 0.1;

    double market_order_prob = 0.1;
    double cancel_order_prob = 0.3;
    double limit_order_prob = 1 - market_order_prob - cancel_order_prob;
};
