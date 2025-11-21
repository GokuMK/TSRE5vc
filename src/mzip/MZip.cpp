/*
 * This file is part of: Generator Plakatowych Zestawie� Poci�g�w.
 * 
 * Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 * 
 * Licensed under GNU General Public License 3.0 or later. 
 * 
 * See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <QDebug>
#include "MZip.h"
#include "miniz/miniz.h"

MZip::MZip() {
}

MZip::MZip(QString name) {
    filename = name;
}

MZip::MZip(const MZip& orig) {
}

MZip::~MZip() {
}

void MZip::loadAllFiles(){
    int i, sort_iter;
    mz_bool status;
    size_t uncomp_size;
    mz_zip_archive zip_archive;
    void *p;
    char *archive_filename;

    const char *s_Test_archive_filename = qstrdup(filename.toLatin1().constData());

    //assert((strlen(s_pTest_str) + 64) < sizeof(data));

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));

    status = mz_zip_reader_init_file(&zip_archive, s_Test_archive_filename, 0);
    if (!status)
    {
      qDebug() << "mz_zip_reader_init_file() failed!";
      return;
    }

    //QStringList files;
    // Get and print information about each file in the archive.
    for (i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); i++)
    {
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat))
      {
         qDebug() << "mz_zip_reader_file_stat() failed!";
         mz_zip_reader_end(&zip_archive);
         return;
      }
      fileNames.push_back(QString::fromLatin1(file_stat.m_filename));
      //printf("Filename: \"%s\", Comment: \"%s\", Uncompressed size: %u, Compressed size: %u, Is Dir: %u\n", file_stat.m_filename, file_stat.m_comment, (uint)file_stat.m_uncomp_size, (uint)file_stat.m_comp_size, mz_zip_reader_is_file_a_directory(&zip_archive, i));

      /*if (!strcmp(file_stat.m_filename, "directory/"))
      {
        if (!mz_zip_reader_is_file_a_directory(&zip_archive, i))
        {
          printf("mz_zip_reader_is_file_a_directory() didn't return the expected results!\n");
          mz_zip_reader_end(&zip_archive);
          return;
        }
      }*/
    }

    // Close the archive, freeing any resources it was using
    mz_zip_reader_end(&zip_archive);
    
    //return;
    
    
    // Now verify the compressed data
    for (sort_iter = 0; sort_iter < 2; sort_iter++)
    {
      memset(&zip_archive, 0, sizeof(zip_archive));
      status = mz_zip_reader_init_file(&zip_archive, s_Test_archive_filename, sort_iter ? MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
      if (!status)
      {
        qDebug() << "mz_zip_reader_init_file() failed!";
        return;
      }

      for (i = 0; i < fileNames.size(); i++)
      {
        archive_filename = qstrdup(fileNames[i].toLatin1().constData());
        //printf(archive_filename);
        //sprintf(archive_filename, "%u.txt", i);
        //sprintf(data, "%u %s %u", (N - 1) - i, s_pTest_str, i);

        // Try to extract all the files to the heap.
        p = mz_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);
        if (!p)
        {
          qDebug() << "mz_zip_reader_extract_file_to_heap() failed!";
          mz_zip_reader_end(&zip_archive);
          return;
        }
        //qDebug() << "Successfully extracted file " << archive_filename << " size " << (uint)uncomp_size;
        //printf("File data: \"%s\"\n", (const char *)p);
        // We're done.
        fileData[fileNames[i]] = new QByteArray((const char *)p, (uint)uncomp_size);
        //qDebug() << fileData[fileNames[i]]->size();
        mz_free(p);
      }

      // Close the archive, freeing any resources it was using
      mz_zip_reader_end(&zip_archive);
    }

    //qDebug() << "Success.";
}
