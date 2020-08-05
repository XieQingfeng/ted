#include "storageCore.hpp"
#include <sys/time.h>

struct timeval timestartStorage;
struct timeval timeendStorage;

extern Configure config;
extern Database fp2ChunkDB;
extern Database fileName2metaDB;

StorageCore::StorageCore()
{
    RecipeNamePrefix_ = config.getRecipeRootPath();
    containerNamePrefix_ = config.getContainerRootPath();
    maxContainerSize_ = config.getMaxContainerSize();
    RecipeNameTail_ = ".recipe";
    containerNameTail_ = ".container";
    ifstream fin;
    fin.open(".StorageConfig", ifstream::in);
    if (fin.is_open()) {
        fin >> lastContainerFileName_;
        fin >> currentContainer_.used_;
        fin.close();

        //read last container
        fin.open(containerNamePrefix_ + lastContainerFileName_ + containerNameTail_, ifstream::in | ifstream::binary);
        fin.read(currentContainer_.body_, currentContainer_.used_);
        fin.close();

    } else {
        lastContainerFileName_ = "abcdefghijklmno";
        currentContainer_.used_ = 0;
    }
    cryptoObj_ = new CryptoPrimitive();
}

StorageCore::~StorageCore()
{
    ofstream fout;
    fout.open(".StorageConfig", ofstream::out);
    fout << lastContainerFileName_ << endl;
    fout << currentContainer_.used_ << endl;
    fout.close();

    string writeContainerName = containerNamePrefix_ + lastContainerFileName_ + containerNameTail_;
    currentContainer_.saveTOFile(writeContainerName);
#if BREAK_DOWN_DEFINE == 1
    cerr << "Upload query DB time = " << queryDBTimeUpload << " s, write Container time = " << writeContainerTime << " s, insert DB time = " << insertDBTimeUpload << " s, unique chunk number = " << uniqueChunkNumber << endl;
    cerr << "Restore chunks DB time = " << queryDBTime << " s, Read Container time = " << readContainerTime << " s, Current read container number = " << readContainerNumber << endl;
#endif
    delete cryptoObj_;
}

bool StorageCore::storeChunks(NetworkHeadStruct_t& networkHead, char* data)
{
    // gettimeofday(&timestartStorage, NULL);
    int chunkNumber;
    memcpy(&chunkNumber, data, sizeof(int));
    int readSize = sizeof(int);
    u_char hash[CHUNK_HASH_SIZE];
    string tmpdata;
#if SEND_CHUNK_LIST_METHOD == 0
    for (int i = 0; i < chunkNumber; i++) {
        int currentChunkSize;
        string originHash(data + readSize, CHUNK_HASH_SIZE);
        readSize += CHUNK_HASH_SIZE;
        memcpy(&currentChunkSize, data + readSize, sizeof(int));
        readSize += sizeof(int);
        if (fp2ChunkDB.query(originHash, tmpdata)) {
            readSize += currentChunkSize;
            continue;
        } else {
            if (!storeChunk(originHash, data + readSize, currentChunkSize)) {
                return false;
            } else {
                readSize += currentChunkSize;
            }
        }
    }
#else
    for (int i = 0; i < chunkNumber; i++) {
        int currentChunkSize;
        Chunk_t newChunk;
        memcpy(&newChunk, data + sizeof(int) + i * sizeof(Chunk_t), sizeof(Chunk_t));
        string originHash((char*)newChunk.chunkHash, CHUNK_HASH_SIZE);
#if BREAK_DOWN_DEFINE == 1
        gettimeofday(&timestartStorage, NULL);
#endif
        bool chunkStatus = fp2ChunkDB.query(originHash, tmpdata);
#if BREAK_DOWN_DEFINE == 1
        gettimeofday(&timeendStorage, NULL);
        queryDBTimeUpload += (1000000 * (timeendStorage.tv_sec - timestartStorage.tv_sec) + timeendStorage.tv_usec - timestartStorage.tv_usec) / 1000000.0;
#endif
        if (chunkStatus) {
            continue;
        } else {
            uniqueChunkNumber++;
            if (!storeChunk(originHash, (char*)newChunk.logicData, newChunk.logicDataSize)) {
                return false;
            }
        }
    }
#endif
    return true;
}

bool StorageCore::restoreRecipesSize(char* fileNameHash, uint64_t& recipeSize)
{
    string recipeName;
    string DBKey(fileNameHash, FILE_NAME_HASH_SIZE);
    if (fileName2metaDB.query(DBKey, recipeName)) {
        ifstream RecipeIn;
        string readRecipeName;
        readRecipeName = RecipeNamePrefix_ + recipeName + RecipeNameTail_;
        RecipeIn.open(readRecipeName, ifstream::in | ifstream::binary);
        if (!RecipeIn.is_open()) {
            std::cerr << "StorageCore : Can not open Recipe file : " << readRecipeName;
            return false;
        } else {
            RecipeIn.seekg(0, std::ios::end);
            recipeSize = RecipeIn.tellg();
            RecipeIn.seekg(0, std::ios::beg);
            RecipeIn.close();
            return true;
        }
    } else {
        std::cerr << "StorageCore : file recipe not exist" << endl;
        return false;
    }
    return true;
}

bool StorageCore::restoreRecipes(char* fileNameHash, u_char* recipeContent, uint64_t& recipeSize)
{
    string recipeName;
    string DBKey(fileNameHash, FILE_NAME_HASH_SIZE);
    if (fileName2metaDB.query(DBKey, recipeName)) {
        ifstream RecipeIn;
        string readRecipeName;
        readRecipeName = RecipeNamePrefix_ + recipeName + RecipeNameTail_;
        RecipeIn.open(readRecipeName, ifstream::in | ifstream::binary);
        if (!RecipeIn.is_open()) {
            std::cerr << "StorageCore : Can not open Recipe file : " << readRecipeName;
            return false;
        } else {
            RecipeIn.seekg(0, std::ios::end);
            recipeSize = RecipeIn.tellg();
            RecipeIn.seekg(0, std::ios::beg);
            RecipeIn.read((char*)recipeContent, recipeSize);
            RecipeIn.close();
            return true;
        }
    } else {
        std::cerr << "StorageCore : file recipe not exist" << endl;
        return false;
    }
    return true;
}

bool StorageCore::storeRecipes(char* fileNameHash, u_char* recipeContent, uint64_t recipeSize)
{

    ofstream RecipeOut;
    string writeRecipeName, buffer, recipeName;

    string DBKey(fileNameHash, FILE_NAME_HASH_SIZE);
    if (fileName2metaDB.query(DBKey, recipeName)) {
        cerr << "StorageCore : current file's recipe exist, modify it now" << recipeName << endl;
        writeRecipeName = RecipeNamePrefix_ + recipeName + RecipeNameTail_;
        RecipeOut.open(writeRecipeName, ios::app | ios::binary);
        if (!RecipeOut.is_open()) {
            std::cerr << "Can not open Recipe file: " << writeRecipeName << endl;
            return false;
        }
        RecipeOut.write((char*)recipeContent, recipeSize);
        RecipeOut.close();
        return true;
    } else {
        char recipeNameBuffer[FILE_NAME_HASH_SIZE * 2 + 1];
        for (int i = 0; i < FILE_NAME_HASH_SIZE; i++) {
            sprintf(recipeNameBuffer + 2 * i, "%02X", fileNameHash[i]);
        }
        cerr << "StorageCore : current file's recipe not exist\nnew recipe file name = " << recipeNameBuffer << endl;
        string recipeNameNew(recipeNameBuffer, FILE_NAME_HASH_SIZE * 2);
        fileName2metaDB.insert(DBKey, recipeNameNew);
        writeRecipeName = RecipeNamePrefix_ + recipeNameNew + RecipeNameTail_;
        RecipeOut.open(writeRecipeName, ios::app | ios::binary);
        if (!RecipeOut.is_open()) {
            std::cerr << "Can not open Recipe file: " << writeRecipeName << endl;
            return false;
        }
        RecipeOut.write((char*)recipeContent, recipeSize);
        RecipeOut.close();
        return true;
    }
}

bool StorageCore::restoreRecipeAndChunk(RecipeList_t recipeList, uint32_t startID, uint32_t endID, ChunkList_t& restoredChunkList)
{

    for (int i = 0; i < (endID - startID); i++) {
        string chunkHash((char*)recipeList[startID + i].chunkHash, CHUNK_HASH_SIZE);
        string chunkData;
        if (restoreChunk(chunkHash, chunkData)) {
            if (chunkData.length() != recipeList[startID + i].chunkSize) {
                cerr << "StorageCore : restore chunk logic data size error" << endl;
                return false;
            } else {
                Chunk_t newChunk;
                newChunk.ID = recipeList[startID + i].chunkID;
                newChunk.logicDataSize = recipeList[startID + i].chunkSize;
                memcpy(newChunk.chunkHash, recipeList[startID + i].chunkHash, CHUNK_HASH_SIZE);
                memcpy(newChunk.logicData, &chunkData[0], newChunk.logicDataSize);
                restoredChunkList.push_back(newChunk);
            }
        } else {
            cerr << "StorageCore : can not restore chunk" << endl;
            return false;
        }
    }
    return true;
}

bool StorageCore::storeChunk(std::string chunkHash, char* chunkData, int chunkSize)
{
    keyForChunkHashDB_t key;
    key.length = chunkSize;
#if BREAK_DOWN_DEFINE == 1
    gettimeofday(&timestartStorage, NULL);
#endif
    bool status = writeContainer(key, chunkData);
#if BREAK_DOWN_DEFINE == 1
    gettimeofday(&timeendStorage, NULL);
    writeContainerTime += (1000000 * (timeendStorage.tv_sec - timestartStorage.tv_sec) + timeendStorage.tv_usec - timestartStorage.tv_usec) / 1000000.0;
#endif
    if (!status) {
        std::cerr << "StorageCore : Error write container" << endl;
        return status;
    }

#if BREAK_DOWN_DEFINE == 1
    gettimeofday(&timestartStorage, NULL);
#endif
    string dbValue;
    dbValue.resize(sizeof(keyForChunkHashDB_t));
    memcpy(&dbValue[0], &key, sizeof(keyForChunkHashDB_t));
    status = fp2ChunkDB.insert(chunkHash, dbValue);
#if BREAK_DOWN_DEFINE == 1
    gettimeofday(&timeendStorage, NULL);
    insertDBTimeUpload += (1000000 * (timeendStorage.tv_sec - timestartStorage.tv_sec) + timeendStorage.tv_usec - timestartStorage.tv_usec) / 1000000.0;
#endif
    if (!status) {
        std::cerr << "StorageCore : Can't insert chunk to database" << endl;
        return false;
    } else {
        currentContainer_.used_ += key.length;
        return true;
    }
}

bool StorageCore::restoreChunk(std::string chunkHash, std::string& chunkDataStr)
{
    keyForChunkHashDB_t key;
    string ans;
#if BREAK_DOWN_DEFINE == 1
    gettimeofday(&timestartStorage, NULL);
#endif

    bool status = fp2ChunkDB.query(chunkHash, ans);
#if BREAK_DOWN_DEFINE == 1
    gettimeofday(&timeendStorage, NULL);
    int diff = 1000000 * (timeendStorage.tv_sec - timestartStorage.tv_sec) + timeendStorage.tv_usec - timestartStorage.tv_usec;
    double second = diff / 1000000.0;
    queryDBTime += second;
#endif
    if (status) {
        memcpy(&key, &ans[0], sizeof(keyForChunkHashDB_t));
        char chunkData[key.length];
#if BREAK_DOWN_DEFINE == 1
        gettimeofday(&timestartStorage, NULL);
#endif

        bool readContainerStatus = readContainer(key, chunkData);
#if BREAK_DOWN_DEFINE == 1
        gettimeofday(&timeendStorage, NULL);
        readContainerTime += (1000000 * (timeendStorage.tv_sec - timestartStorage.tv_sec) + timeendStorage.tv_usec - timestartStorage.tv_usec) / 1000000.0;
#endif
        if (readContainerStatus) {
            chunkDataStr.resize(key.length);
            memcpy(&chunkDataStr[0], chunkData, key.length);
            return true;
        } else {
            cerr << "StorageCore : can not read container for chunk" << endl;
            return false;
        }
    } else {
        cerr << "StorageCore : chunk not in database" << endl;
        return false;
    }
}

bool StorageCore::writeContainer(keyForChunkHashDB_t& key, char* data)
{
    if (key.length + currentContainer_.used_ < maxContainerSize_) {
        memcpy(&currentContainer_.body_[currentContainer_.used_], data, key.length);
        memcpy(key.containerName, &lastContainerFileName_[0], lastContainerFileName_.length());
    } else {
        string writeContainerName = containerNamePrefix_ + lastContainerFileName_ + containerNameTail_;
        currentContainer_.saveTOFile(writeContainerName);
        next_permutation(lastContainerFileName_.begin(), lastContainerFileName_.end());
        currentContainer_.used_ = 0;
        memcpy(&currentContainer_.body_[currentContainer_.used_], data, key.length);
        memcpy(key.containerName, &lastContainerFileName_[0], lastContainerFileName_.length());
    }
    key.offset = currentContainer_.used_;
    return true;
}

bool StorageCore::readContainer(keyForChunkHashDB_t key, char* data)
{
    ifstream containerIn;
    string containerNameStr((char*)key.containerName, lastContainerFileName_.length());
    string readName = containerNamePrefix_ + containerNameStr + containerNameTail_;
    if (containerNameStr.compare(currentReadContainerFileName_) == 0) {
        memcpy(data, currentReadContainer_.body_ + key.offset, key.length);
        return true;
    } else if (containerNameStr.compare(lastContainerFileName_) == 0) {
        memcpy(data, currentContainer_.body_ + key.offset, key.length);
        return true;
    } else {
        readContainerNumber++;
        containerIn.open(readName, std::ifstream::in | std::ifstream::binary);
        if (!containerIn.is_open()) {
            std::cerr << "StorageCore : Can not open Container: " << readName << endl;
            return false;
        }
        containerIn.seekg(0, ios_base::end);
        int containerSize = containerIn.tellg();
        containerIn.seekg(0, ios_base::beg);
        containerIn.read(currentReadContainer_.body_, containerSize);
        if (containerIn.gcount() != containerSize) {
            cerr << "StorageCore : read container error" << endl;
            return false;
        }
        containerIn.close();
        currentReadContainer_.used_ = containerSize;
        memcpy(data, currentReadContainer_.body_ + key.offset, key.length);
        currentReadContainerFileName_ = containerNameStr;
        return true;
    }
}

bool Container::saveTOFile(string fileName)
{
    ofstream containerOut;
    containerOut.open(fileName, std::ofstream::out | std::ofstream::binary);
    if (!containerOut.is_open()) {
        cerr << "Can not open Container file : " << fileName << endl;
        return false;
    }
    containerOut.write(this->body_, this->used_);
    cerr << "Container : save " << setbase(10) << this->used_ << " bytes to file system" << endl;
    containerOut.close();
    return true;
}
