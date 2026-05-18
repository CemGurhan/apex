// EventFeeder is used to generate and feed data from a source to
// a target orderbook.
class EventFeeder {
    virtual void Run() = 0;
    virtual void Stop() = 0;
    virtual ~EventFeeder()  = default;
};
