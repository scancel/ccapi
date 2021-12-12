#include <iostream>
#include <fstream>
#include "ccapi_cpp/ccapi_session.h"
namespace ccapi {
class Benchmark{
 public:
  explicit Benchmark(std::string method){
    this->method = method;
    this->start = UtilTime::now();
  }
  void stop(){
    this->end = UtilTime::now();
  }
  double calculate(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(this->end - this->start).count();
  }
  std::string getMethod(){
    return this->method;
  }
 private:
  std::string method;
  ccapi::TimePoint start;
  ccapi::TimePoint end;
};
class MyLogger final : public Logger {
 public:
  void logMessage(const std::string& severity, const std::string& threadId, const std::string& timeISO, const std::string& fileName,
                  const std::string& lineNumber, const std::string& message) override {
    std::lock_guard<std::mutex> lock(m);
    std::cout << threadId << ": [" << timeISO << "] {" << fileName << ":" << lineNumber << "} " << severity << std::string(8, ' ') << message << std::endl;
  }

 private:
  std::mutex m;
};
MyLogger myLogger;
Logger* Logger::logger = &myLogger;
class BookUpdated : public EventHandler {
 public:
  bool processEvent(const Event& event, Session* session) override {
    if (event.getType() == Event::Type::SUBSCRIPTION_DATA) {
      for (const auto& message : event.getMessageList()) {
        std::cout << std::string("Best bid and ask at ") + UtilTime::getISOTimestamp(message.getTime()) + " are:" << std::endl;
        for (const auto& element : message.getElementList()) {
          const std::map<std::string, std::string>& elementNameValueMap = element.getNameValueMap();
          std::cout << "  " + toString(elementNameValueMap) << std::endl;
        }
      }
    }
    return true;
  }
};
class EventsManager : public EventHandler {
 public:
  void saveBenchmarks(){
    char temp[1000];
    std::string fileName = "benchmarks_" + this->getTime() + ".csv";
    std::map<std::string, Benchmark>::iterator it;
    std::ofstream cached;
    cached.open (fileName);
    cached << "method,durationMS\n";
    for (it = this->benchmarks.begin(); it != this->benchmarks.end(); it++)
    {
      std::string elapsedTime = toString(it->second.calculate());
      cached << it->second.getMethod() + "," + elapsedTime.substr(0, elapsedTime.find(".")) + "\n";
    }
    cached.close();
  }
  bool processEvent(const Event& event, Session* session) override {
//    std::cout << "Received an event:\n" + event.toStringPretty(2, 2) << std::endl;
    std::string uniqueOrderId = "createOrder";
    this->currentSession = session;
    auto eventType = event.getType();
    auto messages = event.getMessageList();
    switch(eventType){
      case Event::Type::SUBSCRIPTION_DATA: {
        Message message = messages.at(0);
        auto messageType = message.getType();
        switch (messageType) {
          case Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE:
            this->handleOrderUpdate(message);
            break;
        case Message::Type::MARKET_DATA_EVENTS_MARKET_DEPTH:
            this->handleBookUpdate(messages);
            break;
        }
        break;
      }
      case Event::Type::SUBSCRIPTION_STATUS: {
        Message message = messages.at(0);
        if (message.getType() == Message::Type::SUBSCRIPTION_STARTED) {
          Request request(Request::Operation::CREATE_ORDER, "deribit", "BTC-PERPETUAL");
          request.appendParam({
              {"SIDE", "BUY"},
              {"LIMIT_PRICE", "2000"},
              {"QUANTITY", "100"},
          });
          session->sendRequest(request);
          this->benchmarks.insert(std::pair<std::string, Benchmark>(uniqueOrderId, Benchmark(uniqueOrderId)));
        }
        break;
      }
      case Event::Type::RESPONSE: {
        Message message = messages.at(0);
        switch (message.getType()){
          case Message::Type::CREATE_ORDER: {
            this->benchmarks.at(uniqueOrderId).stop();
            std::string elapsedTime = toString(this->benchmarks.at(uniqueOrderId).calculate());
            std::cout << "Create order took: " + elapsedTime.substr(0, elapsedTime.find(".")+1) << std::endl;
            break;
          }
          case Message::Type::EDIT_ORDER: {
            std::string uuid = message.getCorrelationIdList()[0];
            this->benchmarks.at(uuid).stop();
            std::string elapsedTime = toString(this->benchmarks.at(uuid).calculate());
            std::cout << "Edit order took: " + elapsedTime.substr(0, elapsedTime.find(".")+1) << std::endl;
            break;
          }
        }
        break;
      }
    }
    return true;
  }
 private:
  Session* currentSession;
  std::map<std::string, Benchmark> benchmarks;
  double bestBid;
  double bestAsk;
  void handleOrderUpdate(Message message){
//    std::cout << "handleOrderUpdate:\n" + message.toStringPretty(2, 2) << std::endl;
    auto element = message.getElementList()[0];
    std::string status = element.getValue(CCAPI_EM_ORDER_STATUS);
    if(status == "open"){
      std::string orderId = element.getValue(CCAPI_EM_ORDER_ID);
      this->editOrder(orderId);
    }
  }
  void handleBookUpdate(std::vector<Message> messages){
    for (const auto& message : messages) {
//      std::cout << std::string("Best bid and ask at ") + UtilTime::getISOTimestamp(message.getTime()) + " are:" << std::endl;
      auto elements = message.getElementList();
      auto bestBid = elements.at(0).getValue("BID_PRICE");
      auto bestAsk = elements.at(1).getValue("ASK_PRICE");
      this->bestAsk = stod(bestAsk);
      this->bestBid = stod(bestBid);
      /*for (const auto& element : elements) {
        const std::map<std::string, std::string>& elementNameValueMap = element.getNameValueMap();
        std::cout << "  " + toString(elementNameValueMap) << std::endl;
      }*/
    }
  }
  std::string getTime()
  {
    timeval curTime;
    gettimeofday(&curTime, NULL);

    int milli = curTime.tv_usec / 1000;
    char buf[sizeof "2011-10-08T07:07:09.000Z"];
    char *p = buf + strftime(buf, sizeof buf, "%FT%T", gmtime(&curTime.tv_sec));
    sprintf(p, ".%dZ", milli);

    return buf;
  }
  void editOrder(std::string orderId){
    std::string uuid = UtilString::generateRandomString(CCAPI_CORRELATION_ID_GENERATED_LENGTH);
    std::string num_text = toString(this->bestBid * 0.5);
    std::string price = num_text.substr(0, num_text.find(".")+1);
    Request request(Request::Operation::EDIT_ORDER, "deribit", "BTC-PERPETUAL", uuid);
    request.appendParam({
        {"ORDER_ID", orderId},
        {"LIMIT_PRICE", price},
        {"QUANTITY", "100"},
    });
    this->currentSession->sendRequest(request);
    this->benchmarks.insert(std::pair<std::string, Benchmark>(uuid, Benchmark("editOrder")));
  }

  std::atomic<bool> fixReady{};
};
} /* namespace ccapi */
using ::ccapi::EventsManager;
using ::ccapi::Request;
using ::ccapi::UtilSystem;
using ::ccapi::UtilTime;
using ::ccapi::BookUpdated;
using ::ccapi::Session;
using ::ccapi::SessionConfigs;
using ::ccapi::SessionOptions;
using ::ccapi::Subscription;
using ::ccapi::toString;
int main(int argc, char** argv) {
  if (UtilSystem::getEnvAsString("DERIBIT_CLIENT_ID").empty()) {
    std::cerr << "Please set environment variable DERIBIT_API_KEY" << std::endl;
    return EXIT_FAILURE;
  }
  if (UtilSystem::getEnvAsString("DERIBIT_CLIENT_SECRET").empty()) {
    std::cerr << "Please set environment variable DERIBIT_API_SECRET" << std::endl;
    return EXIT_FAILURE;
  }
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;
//  BookUpdated bookHandler;
  EventsManager orderHandler;
  Session sessionOrder(sessionOptions, sessionConfigs, &orderHandler);
  Session sessionBook(sessionOptions, sessionConfigs, &orderHandler);
  Subscription subscriptionOrders("deribit", "BTC-PERPETUAL", "ORDER_UPDATE");
  Subscription subscriptionBook("deribit", "BTC-PERPETUAL", "MARKET_DEPTH");
  sessionBook.subscribe(subscriptionBook);
  sessionOrder.subscribe(subscriptionOrders);
  std::this_thread::sleep_for(std::chrono::seconds(60));
//  std::cin.get();
  orderHandler.saveBenchmarks();
  sessionBook.stop();
  sessionOrder.stop();
  std::cout << "Bye" << std::endl;
  return EXIT_SUCCESS;
}
