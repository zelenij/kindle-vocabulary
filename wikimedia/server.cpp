// vim: syntax=cpp
// server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <fstream>
#include <exception>
#include <mutex>
#include <thread>
#include <shared_mutex>
#include <cstdio>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/filesystem.hpp>

#include <arpa/inet.h>

using boost::asio::ip::tcp;

namespace
{
    enum Result
    {
        Success,
        Error
    };
}

class Session;

class Article
{
public:
    Article()
    {
        load();
    }

    size_t getVersion() const
    {
        return mVersion;
    }

    void tryUpdate(size_t expectedVersion, const std::string& newFile)
    {
        // get write lock
        std::unique_lock<std::mutex> lk(mMutex);

        if (mVersion == expectedVersion)
        {
            boost::filesystem::rename(newFile, mFile);

            // Update version
            mVersion++;

            // reload
            load();
        }
    }

    void writeToSession(std::shared_ptr<Session> session);

private:
    // TODO
    const std::string mFile = "/tmp/temp_wiki.html";
    size_t mVersion = 0;

    std::string mContent;

    std::mutex mMutex;

    void load()
    {
        std::ifstream in(mFile);

        in.seekg(0, std::ios::end);   
        mContent.reserve(in.tellg());
        in.seekg(0, std::ios::beg);

        mContent.assign((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
    }

    void writeToSessionSecond(std::shared_ptr<Session> session);

    int fibonacci(int n)
    {
        // TODO - non-recursive
        if (n <= 1 || n == 2)
        {
            return 1;
        }
        else 
        {
            return fibonacci(n - 1) + fibonacci(n - 2);
        }
    }
};

class Environment
{
public:
    bool articleExists(const std::string& article) const
    {
        static const std::string ARTICLE = "Latest_plane_crash";
        return article == ARTICLE;
    }

    Article& getArticle(const std::string& article)
    {
        if (articleExists(article))
        {
            return mArticle;
        }
        else
        {
            throw std::runtime_error("Unsupported article");
        }
    }

private:
    Article mArticle;
};

class Session: public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket socket, Environment& env)
        : mSocket(std::move(socket)),
        mEnv(env)
    {
    }

    void start()
    {
        do_read();
    }

    inline tcp::socket& getSocket()
    {
        return mSocket;
    }

private:
    enum { max_length = 1024 };

    enum Commands
    {
        Get,
        Put
        // Authenticate
    };

    struct PutData: private boost::noncopyable
    {
        PutData() : file(std::tmpnam(NULL)),
            out(file,std::ios::trunc)
        {
        }

        size_t expectedVersion;
        size_t len;
        size_t bytesRead;
        std::string file;
        std::string article;
        std::ofstream out;
    };

    void do_read()
    {
        auto self(shared_from_this());
        mSocket.async_read_some(boost::asio::buffer(mData, max_length),
                [this, self](boost::system::error_code ec, std::size_t length)
                {
                if (!ec)
                {
                    process(length);
                }
                });
    }

    void process(const size_t length)
    {
        switch (static_cast<Commands>(mData[0])) 
        {
            case Get:
                processGet();
                break;

            case Put:
                processPut();
                break;

            default:
                sendError("Unknown command");
                break;
        }
    }

    void processGet()
    {
        getProcessArticle(processGetFinal);
    }

    void processGetFinal(const std::string& article)
    {
        if (mEnv.articleExists(article))
        {
            Article& a = mEnv.getArticle(article);
            // TODO write OK first
            a.writeToSession(shared_from_this());
        }
        else
        {
            sendError("Unknown article");
        }
    }

    void processPut()
    {
        getProcessArticle(processPutSecond);
    }

    void processPutSecond(const std::string& article)
    {
        if (mEnv.articleExists(article))
        {
            // read version
            auto self(shared_from_this());
            mSocket.async_read(boost::asio::buffer(mData, sizeof(size_t) * 2),
                    [this, self, article](boost::system::error_code ec, std::size_t length) 
                    {
                    if (!ec)
                    {
                    std::shared_ptr<PutData> data(new PutData);

                    memcpy(&(data->expectedVersion), mData, sizeof(data->expectedVersion));
                    memcpy(&(data->len), mData + sizeof(data->expectedVersion), sizeof(data->len));

                    data->expectedVersion = ntohl(data->expectedVersion);
                    data->len = ntohl(data->len);
                    data->bytesRead = 0;
                    data->article = article;

                    processPutThird(data);
                    }
                    });
        }
        else
        {
            sendError("Unknown article");
        }
    }

    void processPutThird(std::shared_ptr<PutData> data)
    {
        auto self(shared_from_this());
        mSocket.async_read_some(boost::asio::buffer(mData, sizeof(size_t)),
                [this, self, data](boost::system::error_code ec, std::size_t length) 
                {
                if (!ec)
                {
                    data->out.write(mData, length);

                    if (data->bytesRead + length < data->len)
                    {
                        processPutThird(data);
                    }
                    else
                    {
                        data->out.flush();
                        data->out.close();
                        auto& article = mEnv.getArticle(data->article);
                        article.tryUpdate(data->expectedVersion, data->file);
                    }
                }
                });
    }

    void sendError(const std::string& msg)
    {
        mData[0] = static_cast<char>(Error);
        strncpy(mData + 1, msg.c_str(), max_length - 1);

        do_write(std::min(msg.size() + 1, (size_t)max_length));
    }

    void do_write(std::size_t length)
    {
        auto self(shared_from_this());
        boost::asio::async_write(mSocket, boost::asio::buffer(mData, length),
                [this, self](boost::system::error_code ec, std::size_t /*length*/)
                {
                if (ec)
                {
                // TODO
                }
                });
    }

    tcp::socket mSocket;
    Environment& mEnv;
    char mData[max_length];
};

class server
{
public:
  server(boost::asio::io_service& io_service, short port)
    : mAcceptor(io_service, tcp::endpoint(tcp::v4(), port)),
      mSocket(io_service)
  {
    do_accept();
  }

private:
  void do_accept()
  {
    mAcceptor.async_accept(mSocket,
        [this](boost::system::error_code ec)
        {
          if (!ec)
          {
            std::make_shared<Session>(std::move(mSocket), mEnv)->start();
          }

          do_accept();
        });
  }

  tcp::acceptor mAcceptor;
  tcp::socket mSocket;
  Environment mEnv;
};

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: " << argv[0] << " <port>\n";
            return 1;
        }

        boost::asio::io_service io_service;

        server s(io_service, std::atoi(argv[1]));

        io_service.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

void Article::writeToSession(std::shared_ptr<Session> session)
{
    fibonacci(35);

    // write OK + version + length
    char data[sizeof(size_t) * 2 + 1];

    data[0] = Success;
    char* p = data + 1;
    size_t t = htonl(mVersion);
    memcpy(p, &t, sizeof(size_t));
    p += sizeof(size_t);
    t = htonl(mContent.size());
    memcpy(p, &t, sizeof(size_t));

    boost::asio::async_write(session->getSocket(), boost::asio::buffer(&data, sizeof(data)),
            [this, session](boost::system::error_code ec, std::size_t /*length*/)
            {
            if (!ec)
            {
            writeToSessionSecond(session);
            }
            });
}

void Article::writeToSessionSecond(std::shared_ptr<Session> session)
{
    std::string str;
    {
        std::shared_lock<std::shared_mutex> lk(mMutex);
        str = mContent;
    }

    // write data
    boost::asio::async_write(session->getSocket(), boost::asio::buffer(str.c_str(), str.size()),
            [this, session](boost::system::error_code ec, std::size_t /*length*/)
            {
            if (ec)
            {
            // TODO
            }
            });
}
