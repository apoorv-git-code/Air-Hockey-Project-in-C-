## Air Hockey Project in C 🏒

## Real-Time Multi-Threaded Pong Engine

A high-performance, real-time arcade simulation engine engineered in pure C using POSIX threads (`pthreads`). This architecture converts a traditional, synchronous turn-based game loop into an asynchronous, concurrent pipeline—achieving a sustained 60 FPS frame rate, optimized L1/L2 data cache utilization, and a verified zero-leak dynamic memory profile.

## 🛠️ Architectural Blueprint

The system decouples continuous execution tasks from blocking I/O dependencies by dividing the execution space into three isolated worker threads.

