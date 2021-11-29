#include "ccapi_cpp/ccapi_session.h"
namespace ccapi {
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
  bool processEvent(const Event& event, Session* session) override {
//    std::cout << "Received an event:\n" + event.toStringPretty(2, 2) << std::endl;
    this->currentSession = session;
    auto eventType = event.getType();
    switch(eventType){
      case Event::Type::SUBSCRIPTION_DATA: {
        auto messages = event.getMessageList();
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
        auto message = event.getMessageList().at(0);
        if (message.getType() == Message::Type::SUBSCRIPTION_STARTED) {
          Request request(Request::Operation::CREATE_ORDER, "deribit", "BTC-PERPETUAL");
          request.appendParam({
              {"SIDE", "BUY"},
              {"LIMIT_PRICE", "2000"},
              {"QUANTITY", "100"},
          });
          session->sendRequest(request);
        }
        break;
      }
    }
    if (event.getType() == Event::Type::SUBSCRIPTION_STATUS) {

    }
    return true;
  }
 private:
  Session* currentSession;
  void handleOrderUpdate(Message message){
    std::cout << "handleOrderUpdate:\n" + message.toStringPretty(2, 2) << std::endl;
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
      for (const auto& element : message.getElementList()) {
        const std::map<std::string, std::string>& elementNameValueMap = element.getNameValueMap();
        std::cout << "  " + toString(elementNameValueMap) << std::endl;
      }
    }
  }
  void editOrder(std::string orderId){
    Request request(Request::Operation::EDIT_ORDER, "deribit", "BTC-PERPETUAL");
    request.appendParam({
        {"ORDER_ID", orderId},
        {"LIMIT_PRICE", "3000"},
        {"QUANTITY", "100"},
    });
    this->currentSession->sendRequest(request);
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
//  std::this_thread::sleep_for(std::chrono::seconds(1000));
  std::cin.get();
  sessionBook.stop();
  sessionOrder.stop();
  std::cout << "Bye" << std::endl;
  return EXIT_SUCCESS;
}
