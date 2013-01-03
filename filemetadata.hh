#ifndef FILEMETADATA_HH
#define FILEMETADATA_HH

#include "main.hh"

// ----------------------------------------------------------------------
// to store info about a sharing file 
class FileMetaData {
public:
    FileMetaData(const QString fn); // init according to a file path
    FileMetaData(const QString name, const QByteArray FID, const QString NID):
        fileNameOnly(name), blockListHash(FID), originNID(NID) {}; // init according recv info 
    QString getFileNameWithPath() const {
        return fileNameWithPath;
    }
    QString getFileNameOnly() const {
        return fileNameOnly;
    }
    quint64 getSize() const {
        return size;
    }
    QByteArray getBlockList() const {
        return blockList;
    }
    QByteArray getBlockListHash() const {
        return blockListHash;
    }
    int getSubFilesNum() const {
        return blockList.size() / 32;
    }
    bool contains(QByteArray hash) { // contains FID ?
        if (blockList.indexOf(hash) == -1 && hash != blockListHash) return false;
        else return true;
    }
    bool contains(QString keyWords) const ; // overload for search key words
    QString getFilePath(QByteArray hash) const {
        if ( hash == blockListHash) 
            return metaFileName;
        else 
            return subFileNameList.at((blockList.indexOf(hash)/32));
    }
    QString getOriginNID() const {
        return originNID;
    }
    void fillBlockList(QByteArray &data) {
        blockList = data;
        //qDebug() << "getSubFilesNum " << getSubFilesNum();
        // subFileNameList.reserve(getSubFilesNum());
        for (int i = 0; i < getSubFilesNum(); ++i )
            subFileNameList.append("");
    }
    QString getSubFilePath(int index) const {
        return subFileNameList.at(index);
    }
    void setSubFilePath(const int index, QString path) {
        //qDebug() << index << " " << path;
        subFileNameList.replace(index, path);
    }
    void setMetaFilePath(QString path) {
        metaFileName = path;
    }
    void uniteFile(const QString outDir, const int blockSize);

private:
    void splitFile(const QString outDir, const int blockSize);

private:
    QString fileNameWithPath;
    QString fileNameOnly;
    quint64 size;
    QByteArray blockList;
    QByteArray blockListHash; // File ID
    QStringList subFileNameList; // path of blockHash0, blockHash1, .., blockHashN
    QString metaFileName; // hash file path of File ID
    QString originNID;
};

#endif 
