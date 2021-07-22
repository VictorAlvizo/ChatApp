#include "NetIncludes.h"

//Queue meant to be thread save, as networking works with threading
template<typename T>
class TSQueue {
public:
	TSQueue() = default;
	TSQueue(const TSQueue&) = delete;
	~TSQueue() {
		Clear();
	}
	
	void Clear() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_DeQueue.clear();
	}
	
	void PushBack(const T& data) {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_DeQueue.emplace_back(std::move(data));

		std::unique_lock<std::mutex> ul(m_PushMutex);
		m_WaitCV.notify_one();
	}

	void PushFront(const T& data) {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_DeQueue.emplace_front(std::move(data));

		std::unique_lock<std::mutex> ul(m_PushMutex);
		m_WaitCV.notify_one();
	}

	//Method is meant to hold up a thread until new data is pushed in
	void Wait() {
		while (isEmpty()) { 
			std::unique_lock<std::mutex> ul(m_PushMutex);
			m_WaitCV.wait(ul);
		}
	}

	T PopFront() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		auto t = std::move(m_DeQueue.front()); //Save the item to return it
		m_DeQueue.pop_front();
		return t;
	}

	T PopBack() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		auto t = std::move(m_DeQueue.back());
		m_DeQueue.pop_back();
		return t;
	}

	T Erase(size_t index) {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		auto t = std::move(m_DeQueue[index]);
		m_DeQueue.erase(m_DeQueue.begin() + index);
		return t;
	}

	const T& Front() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		return m_DeQueue.front();
	}

	const T& Back() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		return m_DeQueue.back();
	}

	const T& At(unsigned int index) {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		return m_DeQueue[index];
	}

	bool isEmpty() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		return m_DeQueue.empty();
	}

	size_t count() {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		return m_DeQueue.size();
	}

	const T& operator[](size_t index) {
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		if (index >= m_DeQueue.size()) {
			throw std::exception();
		}

		return m_DeQueue[index];
	}

private:
	std::mutex m_QueueMutex;
	std::mutex m_PushMutex;
	std::condition_variable m_WaitCV;

	std::deque<T> m_DeQueue;
};