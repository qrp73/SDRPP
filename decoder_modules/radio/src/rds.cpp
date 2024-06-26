#include "rds.h"
#include <string.h>
#include <map>
#include <algorithm>

#include <utils/flog.h>

namespace rds {
    std::map<uint16_t, BlockType> SYNDROMES = {
        { 0b1111011000, BLOCK_TYPE_A  },
        { 0b1111010100, BLOCK_TYPE_B  },
        { 0b1001011100, BLOCK_TYPE_C  },
        { 0b1111001100, BLOCK_TYPE_CP },
        { 0b1001011000, BLOCK_TYPE_D  }
    };

    std::map<BlockType, uint16_t> OFFSETS = {
        { BLOCK_TYPE_A,  0b0011111100 },
        { BLOCK_TYPE_B,  0b0110011000 },
        { BLOCK_TYPE_C,  0b0101101000 },
        { BLOCK_TYPE_CP, 0b1101010000 },
        { BLOCK_TYPE_D,  0b0110110100 }
    };

    //                           9876543210
    const uint16_t LFSR_POLY = 0b0110111001;
    const uint16_t IN_POLY   = 0b1100011011;

    const int BLOCK_LEN = 26;
    const int DATA_LEN = 16;
    const int POLY_LEN = 10;

    void RDSDecoder::process(uint8_t* symbols, int count) {
        for (int i = 0; i < count; i++) {
            // Shift in the bit
            shiftReg = ((shiftReg << 1) & 0x3FFFFFF) | (symbols[i] & 1);

            // Skip if we need to shift in new data
            if (--skip > 0) { continue; }

            // Calculate the syndrome and update sync status
            uint16_t syn = calcSyndrome(shiftReg);
            auto synIt = SYNDROMES.find(syn);
            bool knownSyndrome = synIt != SYNDROMES.end();
            sync = std::clamp<int>(knownSyndrome ? ++sync : --sync, 0, 4);
            
            // If we're still no longer in sync, try to resync
            if (!sync) { continue; }

            // Figure out which block we've got
            BlockType type;
            if (knownSyndrome) {
                type = SYNDROMES[syn];
            }
            else {
                type = (BlockType)((lastType + 1) % _BLOCK_TYPE_COUNT);
            }

            // Save block while correcting errors (NOT YET) <- idk why the "not yet is here", TODO: find why
            blocks[type] = correctErrors(shiftReg, type, blockAvail[type]);

            // If block type is A, decode it directly, otherwise, update continous count
            if (type == BLOCK_TYPE_A) {
                decodeBlockA();
            }
            else if (type == BLOCK_TYPE_B) { contGroup = 1; }
            else if ((type == BLOCK_TYPE_C || type == BLOCK_TYPE_CP) && lastType == BLOCK_TYPE_B) { contGroup++; }
            else if (type == BLOCK_TYPE_D && (lastType == BLOCK_TYPE_C || lastType == BLOCK_TYPE_CP)) { contGroup++; }
            else {
                // If block B is available, decode it alone.
                if (contGroup == 1) {
                    decodeBlockB();
                }
                contGroup = 0;
            }

            // If we've got an entire group, process it
            if (contGroup >= 3) {
                contGroup = 0;
                decodeGroup();
            }

            // // Remember the last block type and skip to new block
            lastType = type;
            skip = BLOCK_LEN;
        }
    }

    uint16_t RDSDecoder::calcSyndrome(uint32_t block) {
        uint16_t syn = 0;

        // Calculate the syndrome using a LFSR
        for (int i = BLOCK_LEN - 1; i >= 0; i--) {
            // Shift the syndrome and keep the output
            uint8_t outBit = (syn >> (POLY_LEN - 1)) & 1;
            syn = (syn << 1) & 0b1111111111;

            // Apply LFSR polynomial
            syn ^= LFSR_POLY * outBit;

            // Apply input polynomial.
            syn ^= IN_POLY * ((block >> i) & 1);
        }

        return syn;
    }

    uint32_t RDSDecoder::correctErrors(uint32_t block, BlockType type, bool& recovered) {        
        // Subtract the offset from block
        block ^= (uint32_t)OFFSETS[type];
        uint32_t out = block;

        // Calculate syndrome of corrected block
        uint16_t syn = calcSyndrome(block);

        // Use the syndrome register to do error correction if errors are present
        uint8_t errorFound = 0;
        if (syn) {
            for (int i = DATA_LEN - 1; i >= 0; i--) {
                // Check if the 5 leftmost bits are all zero
                errorFound |= !(syn & 0b11111);

                // Write output
                uint8_t outBit = (syn >> (POLY_LEN - 1)) & 1;
                out ^= (errorFound & outBit) << (i + POLY_LEN);

                // Shift syndrome
                syn = (syn << 1) & 0b1111111111;
                syn ^= LFSR_POLY * outBit * !errorFound;
            }
        }
        recovered = !(syn & 0b11111);

        return out;
    }

    void RDSDecoder::decodeBlockA() {
        // If it didn't decode properly return
        if (!blockAvail[BLOCK_TYPE_A]) { return; }

        // Update timeout
        std::lock_guard<std::mutex> lck(groupMtx);
        auto now = std::chrono::high_resolution_clock::now();
        blockALastUpdate = now;

        // Decode PI code
        piCode = (blocks[BLOCK_TYPE_A] >> 10) & 0xFFFF;
        countryCode = (blocks[BLOCK_TYPE_A] >> 22) & 0xF;
        programCoverage = (AreaCoverage)((blocks[BLOCK_TYPE_A] >> 18) & 0xF);
        programRefNumber = (blocks[BLOCK_TYPE_A] >> 10) & 0xFF;
        decodeCallsign();
    }

    void RDSDecoder::decodeBlockB() {
        // If it didn't decode properly return
        if (!blockAvail[BLOCK_TYPE_B]) { return; }

        // Decode group type and version
        groupType = (blocks[BLOCK_TYPE_B] >> 22) & 0xF;
        groupVer = (GroupVersion)((blocks[BLOCK_TYPE_B] >> 21) & 1);

        // Decode traffic program and program type
        trafficProgram = (blocks[BLOCK_TYPE_B] >> 20) & 1;
        programType = (ProgramType)((blocks[BLOCK_TYPE_B] >> 15) & 0x1F);
    }

    void RDSDecoder::decodeGroup() {
        std::lock_guard<std::mutex> lck(groupMtx);
        auto now = std::chrono::high_resolution_clock::now();

        // Make sure blocks B is available
        if (!blockAvail[BLOCK_TYPE_B]) { return; }

        // Decode block B
        decodeBlockB();
        
        if (groupType == 0) {
            group0LastUpdate = now;
            trafficAnnouncement = (blocks[BLOCK_TYPE_B] >> 14) & 1;
            music = (blocks[BLOCK_TYPE_B] >> 13) & 1;
            uint8_t diBit = (blocks[BLOCK_TYPE_B] >> 12) & 1;
            uint8_t offset = ((blocks[BLOCK_TYPE_B] >> 10) & 0b11);
            uint8_t diOffset = 3 - offset;
            uint8_t psOffset = offset * 2;

            if (groupVer == GROUP_VER_A && blockAvail[BLOCK_TYPE_C]) {
                alternateFrequency = (blocks[BLOCK_TYPE_C] >> 10) & 0xFFFF;
            }

            // Write DI bit to the decoder identification
            decoderIdent &= ~(1 << diOffset);
            decoderIdent |= (diBit << diOffset);

            // Write chars at offset the PSName
            if (blockAvail[BLOCK_TYPE_D]) {
                programServiceName[psOffset] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                programServiceName[psOffset + 1] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
            }
        }
        else if (groupType == 2) {
            group2LastUpdate = now;
            // Get char offset and write chars in the Radiotext
            bool nAB = (blocks[BLOCK_TYPE_B] >> 14) & 1;
            uint8_t offset = (blocks[BLOCK_TYPE_B] >> 10) & 0xF;

            // Clear text field if the A/B flag changed
            if (nAB != rtAB) {
                radioText = "                                                                ";
            }
            rtAB = nAB;

            // Write char at offset in Radiotext
            if (groupVer == GROUP_VER_A) {
                uint8_t rtOffset = offset * 4;
                if (blockAvail[BLOCK_TYPE_C]) {
                    radioText[rtOffset] = (blocks[BLOCK_TYPE_C] >> 18) & 0xFF;
                    radioText[rtOffset + 1] = (blocks[BLOCK_TYPE_C] >> 10) & 0xFF;
                }
                if (blockAvail[BLOCK_TYPE_D]) {
                    radioText[rtOffset + 2] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                    radioText[rtOffset + 3] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
                }
            }
            else {
                uint8_t rtOffset = offset * 2;
                if (blockAvail[BLOCK_TYPE_D]) {
                    radioText[rtOffset] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                    radioText[rtOffset + 1] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
                }
            }
        }
    }

    void RDSDecoder::decodeCallsign() {
        // Determin first better based on offset
        bool w = (piCode >= 21672);
        callsign =  w ? 'W' : 'K';

        // Base25 decode the rest
        std::string restStr;
        int rest = piCode - (w ? 21672 : 4096);
        while (rest) {
            restStr += 'A' + (rest % 26);
            rest /= 26;
        }

        // Pad with As
        while (restStr.size() < 3) {
            restStr += 'A';
        }

        // Reorder chars
        for (int i = restStr.size() - 1; i >= 0; i--) {
            callsign += restStr[i];
        }
    }

    bool RDSDecoder::blockAValid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - blockALastUpdate)).count() < 5000.0;
    }

    bool RDSDecoder::blockBValid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - blockALastUpdate)).count() < 5000.0;
    }

    bool RDSDecoder::group0Valid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - group0LastUpdate)).count() < 5000.0;
    }

    bool RDSDecoder::group2Valid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - group2LastUpdate)).count() < 5000.0;
    }
}