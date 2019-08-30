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

#include <iostream>
#include <functional>

void PrintIndent(int depth)
{
   for (int i = 0; i < depth * 3; i++) {
      std::cout << " ";
   }
}

QXmlStreamReader::TokenType Advance_(QXmlStreamReader& reader)
{
   QXmlStreamReader::TokenType tokenType = reader.readNext();
   if (reader.hasError()) {
      throw QString(reader.errorString());
   }
   return tokenType;
}

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


// bool Parse(QXmlStreamReader& reader, const std::function<void(QXmlStreamReader&)>& childHandler


/*

xxx
bool Advance(QXmlStreamReader& reader, const std::function<void(const QXmlStreamReader&)>& handleChild)
{
   QXmlStreamAttributes att = reader.attributes();
   QStringRef id = att.value("id");

   std::cout << "release id " << id.toLatin1().data() << std::endl;

   bool end = false;
   while (!end) {
      QXmlStreamReader::TokenType tokenType = Advance(reader);
      switch (tokenType) {
         case QXmlStreamReader::StartElement:
            ParseElement(reader, [] (QXmlStreamReader& reader) {
               if (reader.name() == "title") {
                  QString title;
                  ParseElement(reader, [](){}, title);
                  std::cout << "title: " << title.toLatin1().data() << std::endl;
               }
            } );
            break;
         case QXmlStreamReader::EndElement:
            end = true;
            break;
      }
   }
}
*/

/*
bool NextEmbeddedContent(QXmlStreamReader& reader, QString& text)
{
   QXmlStreamReader::TokenType tokenType = Advance(reader);

   bool success = false;

   switch (tokenType) {
      case QXmlStreamReader::StartElement:
         success = true;
         break;
   }
}
*/

/*
void ParseElement(QXmlStreamReader& reader, const std::function<void(const QString&, const QXmlStreamAttributes&, const QString&)>& handler)
{
   bool endElement = false;

   while (!reader.atEnd() && !endElement) {
      QXmlStreamReader::TokenType tokenType = reader.readNext();
      if (reader.hasError()) {
         std::cerr << reader.errorString().toLatin1().data();
         throw 1;
      }

      QString name;
      QXmlStreamAttributes attributes;
      QString text;

      switch (tokenType) {
         case QXmlStreamReader::StartElement: {
            name = reader.name().toString();
            attributes = reader.attributes();
            break;
         }
         case QXmlStreamReader::Characters: {
            text = reader.text().toString(); // Not assuming that there is mixed content.
            break;
         }
         case QXmlStreamReader::EndElement: {
            handler(name, attributes, text);
            endElement = true;
            break;
         }
      }
   }
*/

class Extractor
{
public:
   Extractor(QXmlStreamReader& reader, const QSet<int>& releaseIds);

   QJsonObject Run();

private:
   QXmlStreamReader::TokenType Extractor::Advance();
   void ParseElement(const std::function<void()>& childHandler, QString& text = QString());
   void SkipElement();

   QJsonObject Extractor::ParseRelease();
   QJsonArray  ParseGenres();
   QJsonValue  ParseGenre();

private:
   QXmlStreamReader& reader;
   QSet<int> releaseIds;

   std::function<void(void)> skipElement; //!< Helper: element skipper; bound functor.

};

Extractor::Extractor(QXmlStreamReader& reader, const QSet<int>& releaseIds):
   reader(reader),
   releaseIds(releaseIds),
   skipElement( [this] () { SkipElement(); } )
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
            ParseElement([ this, &releases ] () {
               releases.append(ParseRelease());
            } );
            root.insert("releases", releases);
            if (!reader.atEnd()) {
               throw QString("Expecting end of collection file.");
            }
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

void Extractor::ParseElement(const std::function<void()>& childHandler, QString& text)
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

void Extractor::SkipElement()
{
   ParseElement( [ this ] () {
      SkipElement();
   } );
}

QJsonObject Extractor::ParseRelease()
{
   QJsonObject release;

   QXmlStreamAttributes att = reader.attributes();
   QStringRef id = att.value("id");

   std::cout << "release id " << id.toLatin1().data() << std::endl;

   ParseElement([ this, &release ] () {
                     if (reader.name() == "title") {
                        QString title;
                        ParseElement(skipElement, title);
                        release.insert("title", title);
                     } else if (reader.name() == "genres") {
                        QJsonArray genres = ParseGenres();
                        release.insert("genre", genres);
                     } else {
                        SkipElement();
                     }
               } );

   return release;
}

QJsonArray Extractor::ParseGenres()
{
   // Parse
                        ParseElement( [ this, &genre ] () {
                           if (reader.name() == "genre" && genre.isNull()) {
                              genre = ParseGenre(); // Parse only the first genre entry.
                           } else {
                              SkipElement();
                           }
                        } );

}

QJsonValue Extractor::ParseGenre()
{
   QString genre;
   ParseElement(skipElement, genre);
   return genre;
}

int main(int argc, char* argv[])
{
   QString idsFileName = "c:/Bulk/discogs_20170701_releases.xml.gz/discogs_20170701_releases.xml/vansteve-collection-20170724-1232.csv";
   QSet<int> ids;
   ParseReleaseIds(idsFileName, ids);

   bool verbose = true;

   QString fileName = "c:/Bulk/discogs_20170701_releases.xml.gz/discogs_20170701_releases.xml/discogs_20170701_releases.xml";

   QFile file(fileName);
   if (!file.open(QFile::ReadOnly)) {
      return 1;
   }
   QXmlStreamReader reader(&file);

   Extractor extractor(reader, ids);

   QJsonObject root;
   try {
      root = extractor.Run();

      QJsonDocument doc(root);
      std::cout << doc.toJson().data() << std::endl;

   } catch (const QString& error) {
      std::cout << "Error: " << error.toLatin1().data() << std::endl;
      return 2;
   }
/*
   int depth = 0;
   QStringList stack;
   int currentReleaseId = -1;

   while (!reader.atEnd()) {
      QXmlStreamReader::TokenType tokenType = Advance(reader);
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
            ParseElement(reader, [ &reader, &releases ] (QXmlStreamReader& reader) {
               releases.append(ParseRelease(reader));
            } );
            QJsonObject root;
            root.insert("releases", releases);
            QJsonDocument doc(root);
            std::cout << doc.toJson().data() << std::endl;
            break;
         }
      }
   }
*/
   return 0;
}


/*
int main(int argc, char* argv[])
{
   QString idsFileName = "c:/Bulk/discogs_20170701_releases.xml.gz/discogs_20170701_releases.xml/vansteve-collection-20170724-1232.csv";
   QSet<int> ids;
   ParseReleaseIds(idsFileName, ids);

   bool verbose = true;

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
                     if (id == 31222) {
                        __debugbreak();
                     }
                  }
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