## K8S Control System

This project provides an integrated environment for simulating dynamic systems using C-based plants and Fuzzy Logic controllers implemented in both C++ (FuzzyLite) and Python (Scikit-Fuzzy).

The primary objective of this framework is to evaluate the performance and reliability of distributed controllers within a Kubernetes environment. By containerizing the control logic as independent microservices, the system allows for the analysis of network latency, scalability, and fault tolerance in industrial automation scenarios. The included C-based simulations (car, DC motor, and tank) serve as benchmark plants to validate how fuzzy controller handle real-time feedback loops when deployed across a distributed cluster.