//*****************************************************************************
//!
//! \file
//! \author Stefan Schweitzer
//!
//! (c) Copyright 2017, SICK AG, all rights reserved.
//!
//*****************************************************************************

#include <QFile>
#include <QXmlStreamReader>
#include <QTextCodec>
#include <QStringList>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegExp>

#include <iostream>
#include <functional>

void PrintIndent(int depth)
{
   for (int i = 0; i < depth * 3; i++) {
      std::cout << " ";
   }
}

struct CollectionItem {
   QString folder;
   QString copynote;
};

QHash<int, QList<CollectionItem> > ImportCollectionCsvFile(const QString& fileName)
{
   QHash<int, QList<CollectionItem> > collectionItems;

   QFile file(fileName);
   if (!file.open(QFile::ReadOnly)) {
      throw QString("Cannot read collections .csv file.");
   }

   QStringList lines = QString::fromUtf8(file.readAll()).split("\n");
   lines.pop_front(); // Header.

   QRegExp rxCopyNote("^.*\\[(.+)\\].*$");

   foreach (const QString& line, lines) {

      if (!line.isEmpty()) {

         // std::cout << line.toLatin1().data() << std::endl;
         int commaCount = 0;
         bool inString = false;
         int id;
         CollectionItem item;
         for (int index = 0; index < line.size(); index++) {
            if (line.at(index) == '\"') {
               inString = !inString;
            }
            else if (line.at(index) == ',') {
               if (!inString) {
                  ++commaCount;
                  if (commaCount == 7) {
                     id = ::atoi(line.mid(index + 1).toLatin1().data());
                  } else if (commaCount == 8) {
                     item.folder = line.mid(index + 1);
                     item.folder = item.folder.left(item.folder.indexOf(',')).trimmed();
                     //if (rxComment.exactMatch(line.mid(index + 1))) {
                     //   comment = rxComment.cap(1);
                     //}
                  } else if (commaCount == 12) {
                     QString notes = line.mid(index + 1);
                     notes = notes.left(notes.indexOf(','));
                     if (rxCopyNote.exactMatch(notes)) {
                        item.copynote = rxCopyNote.cap(1);
                     }
                     break;
                  } 
               }
            }
         }
         // std::cout << id << std::endl;

         /*
         QStringList comps;
         comps << QString("discogs %1").arg(id);
         if (!comment.isEmpty()) {
            comps << comment.trimmed();
         }
         commentByRelease.insert(id, comps.join(", "));
         */

         if (collectionItems.contains(id)) {
            collectionItems[id].append(item);
         } else {
            collectionItems.insert(id, QList<CollectionItem>() << item);
         }
         std::cout << "Collection File: Release " << id << " \"" << item.copynote.toLatin1().data() << "\"\n";
      }
   }

   return collectionItems;
}

/*
bool ParseReleaseIds(const QString& fileName, QSet<int>& ids)
{
   QFile file(fileName);
   if (!file.open(QFile::ReadOnly)) {
      return false;
   }

   QStringList lines = QString::fromUtf8(file.readAll()).split("\n");
   lines.pop_front(); // Header.

   foreach(const QString& line, lines) {
      // std::cout << line.toLatin1().data() << std::endl;
      int commaCount = 0;
      bool inString = false;
      int id;
      for (int index = 0; index < line.size(); index++) {
         if (line.at(index) == '\"') {
            inString = !inString;
         }
         else if (line.at(index) == ',') {
            if (!inString) {
               if (++commaCount == 7) {
                  id = ::atoi(line.mid(index + 1).toLatin1().data());
                  break;
               }
            }
         }
      }
      // std::cout << id << std::endl;
      ids.insert(id);
   }

   return true;
}
*/

class Extractor
{
public:
   Extractor(QXmlStreamReader& reader, const QHash<int, QList<CollectionItem> >& collectionItems);

   QJsonObject Run();

private:
   QXmlStreamReader::TokenType Extractor::Advance();
   void ParseCurrentElement(const std::function<void()>& childHandler, QString& text = QString());
   void SkipCurrentElement();

   QJsonObject ParseRelease(int& id);
   QJsonArray  ParseGenres();
   QJsonValue  ParseGenre();
   QJsonValue  ParseArtistsCompound();
   void        ParseArtist(QString& name, QString& join);
   QJsonArray  ParseTracks();
   QJsonObject ParseTrack();
   QJsonValue  ParseReleaseYear();
   void        ParseLabelsAndCatnos(QString& label, QString& catno);
   void        ParseLabelAndCatno(QString& label, QString& catno);

private:
   QXmlStreamReader& reader;
   QHash<int, QList<CollectionItem> > collectionItems;

   std::function<void(void)> skipCurrentElement; //!< Helper: element skipper; bound functor.

   QRegExp rxReleaseDate;
   QRegExp rxReleaseYear;

};

Extractor::Extractor(QXmlStreamReader& reader, const QHash<int, QList<CollectionItem> >& collectionItems):
   reader(reader),
   collectionItems(collectionItems),
   skipCurrentElement( [this] () { SkipCurrentElement(); } ),
   rxReleaseDate("^(\\d\\d\\d\\d)-\\d\\d-\\d\\d$"),
   rxReleaseYear("^(\\d\\d\\d\\d)$")
{
}

QJsonObject Extractor::Run()
{
   QJsonObject root;

   while (!reader.atEnd()) {
      QXmlStreamReader::TokenType tokenType = Advance();
      switch (tokenType) {
         case QXmlStreamReader::StartDocument: {
            QString codecName = reader.documentEncoding().toString();
            if (codecName.isEmpty()) {
               codecName = "UTF-8";
            }
            QTextCodec* codec = QTextCodec::codecForName(codecName.toUtf8());
            if (codec == 0) {
               throw QString("Invalid codec.");
            }
            break;
         }
         case QXmlStreamReader::StartElement: {
            QString name = reader.name().toString();
            if (name != "releases") {
               throw QString("<releases> element not found.");
            }
            QJsonArray releases;
            ParseCurrentElement([ this, &releases ] () {
               int id = -1;
               QJsonObject release = ParseRelease(id);

               if (!release.isEmpty()) {

                  foreach(auto item, collectionItems.value(id)) { // Multiple items per release possibly, hopefully with unique copynotes.

                     QJsonObject releaseInstance = release;

                     if (!item.folder.isEmpty()) {
                        releaseInstance.insert("folder", item.folder);
                     }
                     if (!item.copynote.isEmpty()) {
                        releaseInstance.insert("copynote", item.copynote);
                        std::cout << " \"" << item.copynote.toLatin1().data() << "\"";
                     }

                     releases.append(releaseInstance);
                  }

                  std::cout << std::endl;
               }
            } );
            root.insert("releases", releases);
            break;
         }
      }
   }

   return root;
}

QXmlStreamReader::TokenType Extractor::Advance()
{
   QXmlStreamReader::TokenType tokenType = reader.readNext();
   if (reader.hasError()) {
      throw QString(reader.errorString());
   }
   return tokenType;
}

void Extractor::ParseCurrentElement(const std::function<void()>& childHandler, QString& text)
{
   bool endElement = false;

   while (!reader.atEnd() && !endElement) {
      QXmlStreamReader::TokenType tokenType = Advance();
      switch (tokenType) {
         case QXmlStreamReader::StartElement: {
            childHandler();
            break;
         }
         case QXmlStreamReader::Characters: {
            if (!reader.isWhitespace()) {
               text = reader.text().toString(); // Not assuming that there is mixed content.
            }
            break;
         }
         case QXmlStreamReader::EndElement: {
            endElement = true;
            break;
         }
      }
   }
}

void Extractor::SkipCurrentElement()
{
   ParseCurrentElement( [ this ] () {
      SkipCurrentElement();
   } );
}

QJsonObject Extractor::ParseRelease(int& id)
{
   QJsonObject release;

   QXmlStreamAttributes att = reader.attributes();
   QStringRef idString = att.value("id");
   id = idString.toInt();
//std::cout << id << std::endl;

   //if ((id % 1000) == 0) {
   //   std::cout << "\x0dProcessing " << id << ", " << folderByRelease.count() << " left... ";
   //}

   if (collectionItems.contains(id)) {

      std::cout << "Matched Release " << id;

      try {

         release.insert("id", id);

         ParseCurrentElement([this, &release]() {
            if (reader.name() == "title") {
               QString title;
               ParseCurrentElement(skipCurrentElement, title);
               release.insert("title", title);
               std::cout << " (\"" << title.toLatin1().data() << "\")";
            }
            else if (reader.name() == "genres") {
               release.insert("genres", ParseGenres());
            }
            else if (reader.name() == "artists") {
               release.insert("artist", ParseArtistsCompound());
            }
            else if (reader.name() == "tracklist") {
               release.insert("tracks", ParseTracks());
            }
            else if (reader.name() == "labels") {
               QString label;
               QString catno;
               ParseLabelsAndCatnos(label, catno);
               release.insert("label", label);
               release.insert("catno", catno);
            }
            else if (reader.name() == "released") {
               QJsonValue year = ParseReleaseYear();
               if (!year.toString().isEmpty()) {
                  release.insert("year", year);
               }
            }
            else {
               SkipCurrentElement();
            }
         });

      }
      catch (const QString&) {
         std::cout << "Error parsing release " << idString.toLatin1().data() << "." << std::endl;
         throw;
      }

   } else {
      SkipCurrentElement();
   }

   return release;
}

QJsonArray Extractor::ParseGenres()
{
   QJsonArray genres;

   ParseCurrentElement( [ this, &genres ] () {
      if (reader.name() == "genre") {
         genres.append(ParseGenre());
      } else {
         SkipCurrentElement();
      }
   } );

   return genres;
}

QJsonValue Extractor::ParseGenre()
{
   QString genre;
   ParseCurrentElement(skipCurrentElement, genre);
   return genre;
}

QJsonValue Extractor::ParseArtistsCompound()
{
   QStringList components;

   ParseCurrentElement( [ this, &components ] () {
      if (reader.name() == "artist") {
         QString name;
         QString join;
         ParseArtist(name, join);
         components.append(name);
         components.append(join);
      } else {
         SkipCurrentElement();
      }
   } );

   QString compound;

   if (!components.isEmpty()) {
      components.takeLast(); // Remote last join component.
      compound = components.join(" ");
      compound.replace(" , ", ", ");
   }

   return compound;
}

void Extractor::ParseArtist(QString& name, QString& join)
{
   QString anv;

   ParseCurrentElement( [ this, &name, &anv, &join ] () {
      if (reader.name() == "name") {
         ParseCurrentElement(skipCurrentElement, name);
      } else if (reader.name() == "anv") { // artist name variation
         ParseCurrentElement(skipCurrentElement, anv);
      } else if (reader.name() == "join") {
         ParseCurrentElement(skipCurrentElement, join);
      } else {
         SkipCurrentElement();
      }
   } );

   if (!anv.isEmpty()) {
      name = anv;
   }
}

QJsonArray Extractor::ParseTracks()
{
   QJsonArray tracks;

   ParseCurrentElement( [ this, &tracks ] () {
      if (reader.name() == "track") {
         QJsonObject track = ParseTrack();
         if (!track.isEmpty()) { // There may be invalid pseudo-tracks.
            tracks.append(track);
         }
      } else {
         SkipCurrentElement();
      }
   } );

   return tracks;
}

QJsonObject Extractor::ParseTrack()
{
   QJsonObject track;

   ParseCurrentElement( [ this, &track ] () {
      if (reader.name() == "position") {
         QString position;
         ParseCurrentElement(skipCurrentElement, position);
         track.insert("position", position.toUpper());
      } else if (reader.name() == "title") {
         QString title;
         ParseCurrentElement(skipCurrentElement, title);
         track.insert("title", title);
      } else if (reader.name() == "artists") {
         track.insert("artist", ParseArtistsCompound());
      } else {
         SkipCurrentElement();
      }
   } );

   if (track.value("position").toString().isEmpty()) {
      track = QJsonObject();
   }

   return track;
}

QJsonValue Extractor::ParseReleaseYear()
{
   QString releaseDate;
   ParseCurrentElement(skipCurrentElement, releaseDate);

   QString year;
   if (rxReleaseDate.exactMatch(releaseDate)) {
      year = rxReleaseDate.cap(1);
   } else if (rxReleaseYear.exactMatch(releaseDate)) {
      year = releaseDate;
   //} else if (releaseDate == "?") {
   //   year = releaseDate;
   } else {
      // TODO: output warning! throw QString("Encountered invalid release date: '%1'.").arg(releaseDate);
   }

   return year;
}

void Extractor::ParseLabelsAndCatnos(QString& label, QString& catno)
{
   ParseCurrentElement( [ this, &label, &catno ] () {
      if (reader.name() == "label") {
         if (!label.isNull() || !catno.isNull()) {
            // throw QString("Encountered more than one publisher!");
            SkipCurrentElement();
         } else {
            ParseLabelAndCatno(label, catno);
         }
      } else {
         SkipCurrentElement();
      }
   } );
}

void Extractor::ParseLabelAndCatno(QString& label, QString& catno)
{
   label = reader.attributes().value("name") .toString().trimmed();
   catno = reader.attributes().value("catno").toString().trimmed();

/*
std::cout << reader.name().toLatin1().data() << std::endl;

   ParseCurrentElement( [ this, &name, &cat ] () {
      // std::cout << publisherCompound.toString().toLatin1().data() << std::endl;
      
      if (reader.name() == "name") {
         ParseCurrentElement(skipCurrentElement, name);
      } else if (reader.name() == "catno") {
         ParseCurrentElement(skipCurrentElement, cat);
      } else {
         SkipCurrentElement();
      }
   } );
*/
   if (label.isEmpty()) {
      label = "Unknown";
   }

   SkipCurrentElement();
}

int main(int argc, char* argv[])
{
   QString idsFileName = "vansteve-collection.csv";
   // QSet<int> ids;
   // ParseReleaseIds(idsFileName, ids);
   QHash<int, QList<CollectionItem> > collectionItems = ImportCollectionCsvFile(idsFileName);

   bool verbose = true;

   QString fileName = "discogs_releases.xml";

   QFile file(fileName);
   if (!file.open(QFile::ReadOnly)) {
      return 1;
   }
/*
      QByteArray data = file.read(5 * 50000000);
      data = file.read(2 * 50000000);
      QFile out("discogs_sample.xml");
      out.open(QFile::WriteOnly);
      out.write(data);
      out.close();
      return 0;
*/
   QXmlStreamReader reader(&file);

   Extractor extractor(reader, collectionItems);

   QJsonObject root;
   try {

      root = extractor.Run();

      QJsonDocument doc(root);
      QFile output("collection.json");
      if (output.open(QFile::WriteOnly)) {
         output.write(doc.toJson());
      }

      std::cout << "\nFound all releases that are in the collection." << std::endl;

   } catch (const QString& error) {
      std::cerr << "Error: " << error.toLatin1().data() << std::endl;
      return 2;
   }

   return 0;
}


/*
int main(int argc, char* argv[])
{
   QString idsFileName = "c:/Bulk/discogs_20170701_releases.xml.gz/discogs_20170701_releases.xml/vansteve-collection-20170724-1232.csv";
   QSet<int> ids;
   ParseReleaseIds(idsFileName, ids);

   bool verbose = false;

   QJsonObject objReleases;

   QString fileName = "c:/Bulk/discogs_20170701_releases.xml.gz/discogs_20170701_releases.xml/discogs_20170701_releases.xml";

   QFile file(fileName);
   if (!file.open(QFile::ReadOnly)) {
      return 1;
   }
   QXmlStreamReader reader(&file);

   int depth = 0;
   QStringList stack;
   int currentReleaseId = -1;

   while (!reader.atEnd()) {
      QXmlStreamReader::TokenType tokenType = reader.readNext();
      if (reader.hasError()) {
         std::cerr << reader.errorString().toLatin1().data();
         return 2;
      }
      switch (tokenType) {
         case QXmlStreamReader::StartDocument: {
            QString codecName = reader.documentEncoding().toString();
            if (codecName.isEmpty()) {
               codecName = "UTF-8";
            }
            QTextCodec* codec = QTextCodec::codecForName(codecName.toUtf8());
            if (codec == 0) {
               std::cerr << "Invalid codec." << std::endl;
               return 3;
            }
            break;
         }
         case QXmlStreamReader::StartElement: {
            QString name = reader.name().toString();
            stack.append(name);
            if (verbose) {
               PrintIndent(depth);
               std::cout << "<" << name.toLatin1().data() << ">" << std::endl;
            }
            QXmlStreamAttributes attributes = reader.attributes();
            foreach(auto att, attributes) {
               if (verbose) {
                  PrintIndent(depth + 1);
                  std::cout << att.name().toLatin1().data() << " = " << att.value().toLatin1().data() << std::endl;
                  //if (name == "release" && att.name() == "id" && att.value() == "2126") {
                  //   __debugbreak();
                  //}
               }
               if (stack.join("/") == "releases/release" && att.name() == "id") {
                  int id = att.value().toInt();
                  if (ids.contains(id)) {
                     currentReleaseId = id;
                  }
                  if (id == 31000) verbose = true;
                  if (id == 32000) verbose = false;
               }

            }
            ++depth;
            break;
         }
         case QXmlStreamReader::EndElement: {
            if (verbose) {
               PrintIndent(depth);
               std::cout << "<>" << std::endl;
            }
            --depth;
            stack.takeLast();
            if (stack.join("/") == "releases") {
               currentReleaseId = -1;
            }
            break;
         }
         case QXmlStreamReader::Characters: {
            if (!reader.isWhitespace()) {
               if (stack.join("/") == "releases/release/title" && currentReleaseId > 0) {
                  std::cout << currentReleaseId << " " << reader.text().toString().toLatin1().data() << std::endl;

               }
               if (verbose) {
                  PrintIndent(depth);
                  std::cout << "\"" << reader.text().toString().toLatin1().data() << "\"" << std::endl;
               }
            }
            break;
         }
      }
   }

   return 0;
}



*/