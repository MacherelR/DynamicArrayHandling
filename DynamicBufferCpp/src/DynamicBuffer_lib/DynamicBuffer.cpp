#include "DynamicBuffer.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <vector>

DynamicBuffer::DynamicBuffer(size_t
nVariables,
size_t windowSize
)
:

nVariables (nVariables), windowSize(windowSize),
bufferLength(DEFAULT_BUFFER_LENGTH_FACTOR * windowSize * nVariables),
data(bufferLength, std::nan("")),
counters((DEFAULT_BUFFER_LENGTH_FACTOR * windowSize), 0) {
}

bool DynamicBuffer::deleteRecord(long timestamp) {
    // Find the index for the given timestamp
    auto it = indexes.find(timestamp);
    if (it == indexes.end()) {
        // Timestamp not found
        return false;
    }

    size_t startIndex = it->second;
    size_t endIndex = startIndex + nVariables;

    // Erase the nVariables from data starting from startIndex
    data.erase(data.begin() + startIndex, data.begin() + endIndex);

    // Remove the timestamp entry from indexes
    indexes.erase(it);

    // Adjust subsequent indexes in the map
    for (auto &entry: indexes) {
        if (entry.second > startIndex) {
            entry.second -= nVariables;
        }
    }

    return true;
}

bool DynamicBuffer::addOrUpdateRecord(long timestamp, size_t columnIndex,
                                      double value) {
    bool newEntry = true;
    if (columnIndex >= nVariables) {
        throw std::invalid_argument("Column index out of range");
    }

    size_t dataIndex;
    auto it = indexes.find(timestamp);

    if (it != indexes.end()) {
        newEntry = false;
        // Timestamp exists: update the value directly.
        dataIndex = it->second + columnIndex;
        if (dataIndex < bufferLength) {
            bool isNan = std::isnan(data[dataIndex]);
            data[dataIndex] = value;
            size_t rowIndex = dataIndex / nVariables;
            // size_t latestIndex = indexes.rbegin()->second / nVariables;
            // only increment counter if the row is within the window range
            if (rowIndex > (indexes.rbegin()->second / nVariables - windowSize)) {
                // Only increment counter if the value was NaN before, else it would mean it is an update
                if (isNan) {
                    counters[rowIndex]++;
                }
            }
            variableUpdates[timestamp]++;
        } else {
            throw std::out_of_range("Attempting to write beyond the buffer length");
        }
    } else {
        // Check if there is enough room for a new record
        if (!hasEnoughRoomForNewRecord()) {
            // Remove all rows with zero counters
            removeZeroCount();
            if (!hasEnoughRoomForNewRecord()) {
                throw std::out_of_range("Buffer is full and can't be emptied further.");
            }
        }

        if (indexes.empty() || timestamp > indexes.rbegin()->first) {
            dataIndex =
                    (!indexes.empty()) ? (indexes.rbegin()->second + nVariables) : 0;
        } else {
            auto nextTimestampIt = indexes.upper_bound(timestamp);
            dataIndex =
                    (nextTimestampIt != indexes.end()) ? nextTimestampIt->second : 0;

            // Shift existing data to make room for the new record
            std::move_backward(data.begin() + dataIndex, data.end() - nVariables,
                               data.end());

            // Update indexes for all timestamps after the new one
            for (auto iter = nextTimestampIt; iter != indexes.end(); ++iter) {
                iter->second += nVariables;
            }
        }
        // Insert the new value
        std::fill_n(data.begin() + dataIndex, nVariables,
                    std::nan("")); // Prepare space for new data
        data[dataIndex + columnIndex] =
                value; // Insert new value at the correct column

        // Update the index for the new timestamp
        indexes[timestamp] = dataIndex;

        // Update the counters appropriately
        size_t rowIndex = dataIndex / nVariables;
        counters.resize(std::max(counters.size(), rowIndex + 1),
                        0); // Ensure counters vector is large enough
        // only increment counter if the row is within the window range
        if (rowIndex > (indexes.rbegin()->second / nVariables - windowSize)) {
            counters[rowIndex] = 1;
        }

        variableUpdates[timestamp] = 1;
    }

    return newEntry;
}

void DynamicBuffer::print() const {
    // Debug method to print the contents of the buffer
    for (const auto &pair: indexes) {
        long timestamp = pair.first;
        size_t startIndex = pair.second;

        std::cout << timestamp << ": ";

        for (size_t i = 0; i < nVariables; ++i) {
            if (startIndex + i < data.size()) {
                std::cout << data[startIndex + i] << ", ";
            }
        }

        std::cout << std::endl;
    }
}

std::vector<double> DynamicBuffer::getRecordByTimestamp(long timestamp) const {
    auto it = indexes.find(timestamp);
    if (it == indexes.end()) {
        throw std::invalid_argument("Timestamp not found");
    }
    auto index = it->second;
    return std::vector<double>(data.begin() + index,
                               data.begin() + index + nVariables);
}

std::vector<double> DynamicBuffer::getRecordByIndex(size_t
index) const {
if (index >= indexes.

size()

) {
throw std::out_of_range("Index out of range");
}
return
std::vector<double>(data
.

begin()

+ index,
data.

begin()

+ index + nVariables);
}

const double *DynamicBuffer::getRecordByTimestampPtr(long timestamp,
                                                     size_t &outSize) const {
    auto it = indexes.find(timestamp);
    if (it != indexes.end()) {
        size_t startIndex = it->second;
        // Ensure it doesn't exceed the data vector size
        if (startIndex + nVariables <= data.size()) {
            outSize = nVariables;
            return &data[startIndex];
        }
    }
    outSize = 0;
    return nullptr;
}

const double *DynamicBuffer::getRecordByIndexPtr(size_t
index) const {
size_t startIndex = index * nVariables;

if (index < (
windowSize *DEFAULT_BUFFER_LENGTH_FACTOR
) &&
startIndex + nVariables <= data.

size()

) {
return &data[startIndex];
}

return
nullptr;
}

const double *DynamicBuffer::getSlice(long timestamp, size_t N,
                                      size_t &outSize) const {
    auto it = indexes.find(timestamp);
    if (it != indexes.end()) {
        size_t targetIndex = it->second;

        size_t startIndex = (N > (targetIndex / nVariables + 1))
                            ? 0
                            : targetIndex - (N - 1) * nVariables;

        // Calculate the size of the slice in terms of number of doubles
        outSize = std::min(data.size() - startIndex, N * nVariables);

        // Check if the calculated slice exceeds the bounds of the data vector
        if (startIndex + outSize <= data.size()) {
            return &data[startIndex];
        }
    }
    outSize = 0;
    return nullptr; // Return nullptr if the request cannot be fulfilled
}

std::vector<long> DynamicBuffer::getSliceTimestamps(long timestamp,
                                                    size_t N) const {
    std::vector<long> timestamps;
    auto it = indexes.find(timestamp);
    if (it != indexes.end()) {
        size_t targetIndex = it->second;
        size_t startIndex = (N > (targetIndex / nVariables + 1))
                            ? 0
                            : targetIndex - (N - 1) * nVariables;
        size_t endIndex = std::min(startIndex + N * nVariables, data.size());

        for (auto &pair: indexes) {
            if (pair.second >= startIndex && pair.second < endIndex) {
                timestamps.push_back(pair.first);
            }
        }
    }
    return timestamps;
}

size_t DynamicBuffer::getNVariables() const { return nVariables; }

void DynamicBuffer::removeFront(size_t
removeCount) {
size_t originalSize = data.size();
size_t elementsToRemove = removeCount * nVariables;
if (removeCount >= originalSize) {
// Clear the vector if removing more or equal elements than contained
data.

clear();

counters.

clear();

// Fill the entire vector with NaN, maintaining original size
data.
resize(originalSize, std::nan("")
);
counters.
resize((DEFAULT_BUFFER_LENGTH_FACTOR
* windowSize), 0);
// Clear indexes map as all elements are removed
indexes.

clear();

variableUpdates.

clear();

} else {
// Move the remaining elements to the beginning
std::move(data
.

begin()

+ elementsToRemove, data.

end(), data

.

begin()

);
// Resize the vector down, then back up to original size, filling with NaNs
data.
resize(originalSize
- elementsToRemove);
std::fill_n(std::back_inserter(data), elementsToRemove, std::nan("")
);

std::move(counters
.

begin()

+ removeCount, counters.

end(), counters

.

begin()

);
counters.
resize((DEFAULT_BUFFER_LENGTH_FACTOR
* windowSize), 0);
// Adjust the indexes map:
// Create a new map to store updated indexes
std::map<long, size_t> updatedIndexes;
for (
const auto &pair
: indexes) {
if (pair.second >= elementsToRemove) {
updatedIndexes[pair.first] = pair.second -
elementsToRemove;
}
}
// Swap the updated map with the old one
indexes.
swap(updatedIndexes);

// Adjust the variableUpdates map:
auto varUpdatesIt = variableUpdates.begin();
// advance iterator
for (
size_t i = 0;
i<removeCount && varUpdatesIt != variableUpdates.

end();

++i) {
++
varUpdatesIt;
}
// Erase updates map entries
variableUpdates.
erase(variableUpdates
.

begin(), varUpdatesIt

);
}
}

long DynamicBuffer::minKey() const {
    if (indexes.empty()) {
        std::cerr << "Error: No data available." << std::endl;
        return -1;
    }
    return indexes.begin()->first;
}

long DynamicBuffer::maxKey() const {
    if (indexes.empty()) {
        std::cerr << "Error: No data available." << std::endl;
        return -1;
    }
    return indexes.rbegin()->first;
}

size_t DynamicBuffer::getNumRows() const { return indexes.size(); }

bool DynamicBuffer::hasEnoughRoomForNewRecord() {
    if (indexes.size() < (DEFAULT_BUFFER_LENGTH_FACTOR * windowSize))
        return true;

    return false;
}

void DynamicBuffer::removeZeroCount() {
    int nZeros = countSubsequentZerosCounters();

    if (nZeros > 0) {
        removeFront(nZeros);
    }
}

size_t DynamicBuffer::countSubsequentZerosCounters() {
    size_t count = 0;
    for (auto value: counters) {
        if (value == 0) {
            ++count;
        } else {
            break;
        }
    }
    return count;
}

void DynamicBuffer::decrementCounters(const std::vector<long> &timestamps) {
    for (long timestamp: timestamps) {
        auto it = indexes.find(timestamp);
        if (it != indexes.end()) {
            size_t index = it->second;
            size_t counterIndex = index / nVariables;
            if (counterIndex < counters.size() && counters[counterIndex] > 0) {
                counters[counterIndex]--;
            }
        }
    }
}

void DynamicBuffer::printCounters() const {
    std::cout << "Counters: ";
    for (auto value: counters) {
        std::cout << value << " ";
    }
    std::cout << std::endl;
}

std::vector<int> DynamicBuffer::getCounters() const { return counters; }

void DynamicBuffer::printIndexes() const {
    std::cout << "Indexes: ";
    for (auto pair: indexes) {
        std::cout << pair.first << " : " << pair.second << " | ";
    }
    std::cout << std::endl;
}

void DynamicBuffer::printData() const {
    std::cout << "Data: [";
    for (auto value: data) {
        std::cout << value << " ";
    }
    std::cout << "]" << std::endl;
}

size_t DynamicBuffer::getVariableUpdateCount(long timestamp) {
    return variableUpdates[timestamp];
}

