# README for Wikimedia

There were two general approaches to the task:

* Use an existing HTTP container, from Apache/Nginx to any Java-based solution, and build the project on top.
* Use a purpose built C/C++ solution

I decided to go with the second option.  Setting up a container/web server is a task which would take around half an hour out of 2 hours available.  Also, I wanted to implement a low level C++ server since the position is C++ oritented.

I based my solution on boost asio library (asynchronous I/O).  Rather than implementing an HTTP handler, I designed my own, very much simplified protocol.

## Main features of the solution:

* All I/O operations are non-blocking
* The server is multithreaded (provied by boost::asio)
* The server allows multiple reads simultaneosly.  It only locks exclusively the file with the article briefly when it is about to update it
* The server decides if update operation should go through based on the version of the file.  Each time the article is updated, the version is incremented.  An editor provides the server with the version number as well as the updated version.  Only if this version matches the one currently on the server, the update will go through.  This prevents updates based on older version getting in.  Of course, this is not a perfect solution, but quite a reasonable one

## What doesn't work yet

* One small function's implementation is missing
* There are a few compilation errors, nothing too complicated

## What can be improved

* Add authentication for updates
* On update, optimize data transfer from session to the article object
* Improve internal storage and sending of articles - don't require continuous block of memory via std::string, as now
* Refactor code to make it clearer
