# CFNAgg: Hierarchical Data Aggregation in ndnSIM

**CFNAgg** is a hierarchical data aggregation system implemented in ndnSIM 2.9 (NDN Forwarding Daemon 22.02). It allows a tree of NDN nodes to aggregate data as Interests propagate from a root node to multiple leaves and Data flows back up, combining intermediate results. This README provides an overview of each component, instructions to build and run the simulation, input file formats, and configuration options.

## Components Overview

- **CFNProducerApp** (leaf producer application): Runs on leaf nodes (data sources). It registers a unique prefix (e.g., corresponding to the node name) and responds to incoming Interests with Data. The Data carries a payload containing a fixed 64-bit value (e.g., a sensor reading). By default each producer returns the value `1`, but this can be configured via attributes.

- **CFNAggregatorApp** (intermediate aggregator application): Runs on intermediate (non-leaf, non-root) nodes. It registers its own prefix and on receiving an Interest from its parent, it **forwards Interests to each of its children** and waits for their Data. As child Data arrives, it maintains a partial aggregate (by default, summing child values). Once all children respond (or a timeout occurs), it produces a single aggregated Data packet (summing all child values received) to satisfy the parent's Interest. If some children did not respond before the timeout, it performs a **partial aggregation** (using only the responses received) so that the parent still gets a timely Data packet.

- **CFNRootApp** (root aggregator application): Runs on the root node of the aggregation tree. The root initiates the data collection by sending Interests to its children. It does not have a parent to respond to; instead, it logs the final aggregated result. The root uses a **congestion control algorithm** to manage how many Interests (aggregation rounds) are in flight concurrently:
  - *AIMD* – Additive-Increase Multiplicative-Decrease (similar to TCP Reno).
  - *CUBIC* – TCP Cubic congestion control.
  - *BBR* – Bottleneck Bandwidth and RTT (a simplified implementation).
  
  The root treats each complete aggregation round (an Interest sent to all children and the corresponding Data aggregated) as one "flow" for congestion-control purposes. It increases or decreases its sending window (number of parallel rounds) based on acknowledgments (child Data) or timeouts.

- **AggregationBuffer**: A helper structure used by aggregator and root apps to track the state of an ongoing aggregation round (identified by a sequence number). It stores how many child responses are expected vs. received, the partial sum of received values, a boolean vector marking which specific children have responded, and a scheduled timeout event. There is one AggregationBuffer per outstanding Interest sequence at an aggregator/root.

- **StragglerManager**: A utility module that schedules and handles **timeouts for straggling children**. When an aggregator (root or intermediate) forwards Interests to its children, it uses StragglerManager to schedule a timeout event (after a configured period, e.g., 1 second by default). If the event triggers before all children respond, the aggregator’s `OnStragglerTimeout` callback runs: this will finalize the aggregation with whatever data has arrived (partial result) and log/send the result upward. If all children respond in time, the aggregator cancels the timeout event.

- **CongestionControl** interface and implementations: Defines a common interface (`OnData`, `OnTimeout`, `GetCwnd`) for controlling the sending rate at the root. Three algorithms are provided:
  - **AIMD**: Starts with `cwnd=1`. Each successful round (all children replied) increases the window by 1 (or by 1 per RTT in congestion avoidance), and each timeout reduces the window to half (multiplicative decrease, minimum 1).
  - **CUBIC**: Uses the Cubic function to adjust `cwnd` over time. After a loss, it multiplies the window by β (0.7) and then gradually increases following a cubic curve. This implementation is simplified but captures the key behaviors (faster growth after recovery, less drastic reduction on loss than AIMD).
  - **BBR**: Attempts to set `cwnd` based on estimated Bandwidth-Delay Product. It monitors the rate of data received and an estimated minimum RTT to adjust the window. This implementation is a **basic placeholder** and not a full BBR; it will increase `cwnd` when throughput increases and decrease slightly on timeouts, aiming to approximate stable sending at the available bandwidth.

- **TraceCollector**: A logging utility that records key events to an output file (`cfnagg-trace.csv` by default). It logs:
  - When the root sends an Interest (including the sequence number and time).
  - When any node receives a Data from a child (identifying which child responded and at what time).
  - When an aggregation is completed at an aggregator or root, either fully (`AggregateComplete` event) or partially due to timeout (`AggregatePartial`). It logs how many children were expected vs. received and the final aggregated value.
  
  These logs can be used to verify correctness (e.g., check that the final sum equals the sum of all leaf values that responded) and to measure performance (latencies, throughput, etc.).

## Building and Running

**Prerequisites:** ndnSIM 2.9 must be installed (which in turn is based on NS-3). This code should be placed in an ndnSIM-compatible project (for example, under `ns-3/src/ndnSIM/apps/` for the apps, or in the `scratch/` directory for quick experiments).

**File structure:** All source files (`*.cpp` and `*.hpp`) for CFNAgg should be added to your NS-3/ndnSIM project. For instance, you might create a directory `src/ndnSIM/examples/cfnagg/` and place the files there, or simply use the `scratch/` folder for a quick simulation. Ensure the files are referenced in your Waf build scripts if added as a new module.

**Compilation:** Use NS-3’s build system (Waf) to compile. For example, from the NS-3 top-level directory run:
```bash
./waf configure --enable-examples
./waf
