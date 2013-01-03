#include "filemetadata.hh"

// ---------------------------------------------------------------------
// whether file name contains the key words?
bool FileMetaData::
contains(QString keyWords) const { // overload for search key words
        QStringList list = keyWords.split(QRegExp("\\s+"));
        bool contains = false;
        for (int i = 0; i < list.size(); ++i)
        {
            // all comparisons are in lowercase
            if (fileNameOnly.toLower().indexOf(QString(list.at(i).toLocal8Bit().constData()).toLower()) != -1) contains = true;
        }
        return contains;
}

// ---------------------------------------------------------------------
// constructor for FileMetaData when given a file name
FileMetaData::
FileMetaData(const QString fn): 
    fileNameWithPath(fn), 
    fileNameOnly(QDir(fileNameWithPath).dirName()) { 
    // split the file to ./tmp_blocks/
    splitFile(QObject::tr("tmp_blocks/"), 8*1024);
    // qDebug() << getSubFilesNum();
}


// ---------------------------------------------------------------------
// combine sub files into a unite one
void FileMetaData::
uniteFile(const QString outDir, const int blockSize) {
    QFile unitedFile(outDir + getFileNameOnly());
    unitedFile.open(QIODevice::WriteOnly);
    QDataStream unitedFlow(&unitedFile);
    char buffer[blockSize];
    int part = 0;
    while(QFile::exists(outDir + getFileNameOnly() + QObject::tr("_") + QString::number(part))){
        QFile qfOut(outDir + getFileNameOnly() + QObject::tr("_") + QString::number(part));
        qfOut.open(QIODevice::ReadOnly);
        QDataStream fout(&qfOut);
        int readSize = fout.readRawData(buffer, blockSize);
        qfOut.close();
        unitedFlow.writeRawData(buffer, readSize);
        QFile::remove(outDir + getFileNameOnly() + QObject::tr("_") + QString::number(part));
        ++part;
    }
    unitedFile.close();
}

// ---------------------------------------------------------------------
// split the file into blocks stored in outDir
void FileMetaData::
splitFile(const QString outDir, const int blockSize) {
    QFile qf(fileNameWithPath);
    if (!qf.open(QIODevice::ReadOnly)) {
        qDebug() << "No such file";
        return;
    }
    size = qf.size();
    QDataStream fin(&qf);
    char buffer[blockSize];
    int part = 0;
    QCA::Hash shaHash("sha256");
    do {
        // split one by one
        int readSize = fin.readRawData(buffer, blockSize);
        QFile qfOut(outDir + fileNameOnly + QObject::tr("_") + QString::number(part));
        if (qfOut.open(QIODevice::WriteOnly)) {
            QDataStream fout(&qfOut);
            fout.writeRawData(buffer, readSize);
            qfOut.close();
        }
        // save path to subFileNameList;
        subFileNameList.append((outDir + fileNameOnly + QObject::tr("_") + QString::number(part)));
        // hash
        QFile qfHash(outDir + fileNameOnly + QObject::tr("_") + QString::number(part));
        if (qfHash.open(QIODevice::ReadOnly)) {
            QByteArray data = qfHash.readAll();
            shaHash.update(data);
            //shaHash.update(&qfHash);
            QCA::MemoryRegion hashA = shaHash.final();
            qDebug() << QCA::arrayToHex(hashA.toByteArray());
            // save to blockList
            blockList.append(hashA.toByteArray());
            // test 
            // qDebug() << "path:" << getFilePath(hashA.toByteArray());
            qfHash.close();
            shaHash.clear();
        }
        ++part;
    } while (!fin.atEnd());
    qf.close();
    // calculate the hash of blockList
    //shaHash.update(blockList);
    //QCA::MemoryRegion hashA = shaHash.final();
    //shaHash.clear();
    //qDebug() << "meta : " << QCA::arrayToHex(hashA.toByteArray());
    QFile qfOut(outDir + fileNameOnly + QObject::tr("_meta"));
    metaFileName = outDir + fileNameOnly + QObject::tr("_meta");
    if (qfOut.open(QIODevice::WriteOnly)) {
        //QDataStream fout(&qfOut);
        //fout << blockList;
        qfOut.write(blockList.data(), blockList.size()); // use this because stream would add extra 4 bytes
        qfOut.close();
    }
    // test the file's hash 
    QFile readMeta(metaFileName);
    QByteArray hashAll;
    if (readMeta.open(QIODevice::ReadOnly)) {
        shaHash.update(&readMeta);
        hashAll = shaHash.final().toByteArray();
        qDebug() << QCA::arrayToHex(hashAll);
        readMeta.close();
    }
    blockListHash.append(hashAll);
    // qDebug() << "path:" << getFilePath(hashA.toByteArray());

}


