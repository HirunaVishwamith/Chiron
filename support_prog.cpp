#include <iostream>
#include <iomanip>
#include <bitset>
#include <string>
#include <cstdint>
#include <sstream>

const int TAG_BITS = 23;
const int NUM_TAGS = 4;

class CacheAddressDecoder {
private:
    uint32_t address;
    
public:
    CacheAddressDecoder(uint32_t addr) : address(addr) {}
    
    uint32_t getTag() const {
        return (address >> 13);
    }
    
    uint32_t getCacheLineNumber() const {
        return ((address >> 8) & 0x7F);
    }
    
    uint32_t getWayNumber() const {
        return ((address >> 6) & 0x3);
    }
    
    uint32_t getInCacheLine() const {
        return (address & 0x3F);
    }
};

std::string hexToBinary(const std::string& hex) {
    std::stringstream binary;
    for (char c : hex) {
        int value = std::stoi(std::string(1, c), nullptr, 16);
        binary << std::bitset<4>(value);
    }
    return binary.str();
}

void printAsFiveHex(std::bitset<19>& bs) {
    uint32_t value = bs.to_ulong();
    std::cout << std::hex << std::uppercase;
    for(int i = 4; i >= 0; --i) {
        uint32_t nibble = (value >> (i * 4)) & 0xF;
        std::cout << nibble;
        if(i > 0) std::cout << " ";
    }
}

void splitHexIntoWords(const std::string &hexString) {
    // Ensure the input length is correct (128 hex characters = 64 bytes)
    if (hexString.length() != 128) {
        std::cerr << "Error: Input must be exactly 128 hex characters long (64 bytes)." << std::endl;
        return;
    }

    std::cout << "Split words:" << std::endl;
    
    // Split into 16 words (each word = 8 hex characters = 4 bytes)
    for (int i = 0; i < 16; ++i) {
        std::string word = hexString.substr(128 - (i + 1) * 8, 8); // Extract from the right
        std::cout << "Word " << i*4 << ": 0x" << word << std::endl;
    }
}

void decodeTag(const std::string& hexInput) {
    std::string binaryString = hexToBinary(hexInput);

    std::cout << "\nDecoded Tag Components:\n";
    std::cout << "---------------------------------------------------------------\n";
    std::cout << std::left << std::setw(15) << "Field" << " | " 
              << std::setw(10) << "Decimal" << " | " 
              << "Binary\n";
    std::cout << "---------------------------------------------------------------\n";

    // Calculate starting position from the right
    size_t startPosition = binaryString.length() - TAG_BITS;

    // Extract the tag bits
    std::string tagSection = binaryString.substr(startPosition, TAG_BITS);
    uint32_t tagValue = std::bitset<19>(binaryString.substr(binaryString.length() - 19, 19)).to_ulong();
    uint32_t validBit = tagSection[19] - '0';
    uint32_t modifyBit = tagSection[20] - '0';
    uint32_t sharedBit = tagSection[21] - '0';
    uint32_t plruBit = tagSection[22] - '0';

    std::cout << "Tag" << " = " << std::hex << stoi(tagSection, 0, 2) << std::endl;

    std::cout << "Tag Value" << "       | " << std::setw(10)  << std::bitset<19>(tagValue)  << "\n";
    std::cout << "Valid Bit" << "       | " << std::setw(10) << validBit << "\n";
    std::cout << "Modify Bit" << "      | " << std::setw(10) << modifyBit << "\n";
    std::cout << "Shared Bit" << "      | " << std::setw(10) << sharedBit << "\n";
    std::cout << "PLRU Bit" << "        | " << std::setw(10) << plruBit << "\n";
    std::cout << "---------------------------------------------------------------\n";
}

void decodeAddress(uint32_t hexValue) {
    CacheAddressDecoder decoder(hexValue);
    std::cout << "\nDecoded Address Components:\n";
    std::cout << "-----------------------------------------------------\n";
    std::cout << std::left << std::setw(25) << "Field" << " | " 
              << std::setw(10) << "Decimal" << " | " 
              << "Binary\n";
    std::cout << "-----------------------------------------------------\n";

    std::cout << std::left << std::setw(25) << "Tag (19 bits)" << " | " 
              << std::setw(10) << decoder.getTag() << " | ";
    std::cout << std::bitset<19>(decoder.getTag()) << "\n";

    std::cout << std::left << std::setw(25) << "Cache Line (7 bits)" << " | " 
              << std::setw(10) << decoder.getCacheLineNumber() << " | ";
    std::cout << std::bitset<7>(decoder.getCacheLineNumber()) << "\n";

    std::cout << std::left << std::setw(25) << "Way Number (2 bits)" << " | " 
              << std::setw(10) << decoder.getWayNumber() << " | ";
    std::cout << std::bitset<2>(decoder.getWayNumber()) << "\n";

    std::cout << std::left << std::setw(25) << "In Cache Line (6 bits)" << " | " 
              << std::setw(10) << decoder.getInCacheLine() << " | ";
    std::cout << std::bitset<6>(decoder.getInCacheLine()) << "\n";
    std::cout << "-----------------------------------------------------\n";
}

int main() {
    std::cout << "Choose an option:\n";
    std::cout << "1 - Address Decode\n";
    std::cout << "2 - Tag Decode\n";
    std::cout << "3 - DataBRAM Decode \n";
    std::cout << "Enter your choice: ";
    
    int choice;
    std::cin >> choice;
    
    if (choice == 1) {
        uint32_t hexValue;
        std::cout << "Enter a 32-bit hexadecimal value: ";
        std::cin >> std::hex >> hexValue;
        decodeAddress(hexValue);
    } else if (choice == 2) {
        std::string hexInput;
        std::cout << "Enter 23-bit tag data in hexadecimal: ";
        std::cin >> hexInput;
        decodeTag(hexInput);
    } else if (choice == 3) {
        std::string hexInput;
        std::cout << "Enter 64 byte data in hexadecimal: ";
        std::cin >> hexInput;
        splitHexIntoWords(hexInput);
    } else {
        std::cout << "Invalid choice.\n";
    }
    return 0;
}
