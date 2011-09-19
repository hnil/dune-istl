// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_MATRIXMARKET_HH
#define DUNE_MATRIXMARKET_HH

#include <ostream>
#include <istream>
#include <fstream>
#include <sstream>
#include <limits>
#include <ios>
#include "matrixutils.hh"
#include "bcrsmatrix.hh"
#include "owneroverlapcopy.hh"
#include <dune/common/fmatrix.hh>
#include <dune/common/tuples.hh>
#include <dune/common/misc.hh>

namespace Dune
{

  /**
   * @defgroup ISTL_IO IO for matrices and vectors.
   * @ingroup ISTL_SPMV
   * @brief Provides methods for reading and writing matrices and vectors
   * in various formats.
   *
   *
   * Routine printmatix prints a (sparse matrix with all entries (even zeroes).
   * Function printvector prints a vector to a stream.
   * PrintSparseMatrix prints a sparse matrix omitting all nonzeroes.
   * With writeMatrixToMatlab one can write a matrix in a Matlab readable format.
   * Using storeMartrixMarket and loadMatrixMarket one can store and load a parallel ISTL
   * matrix in MatrixMarket format. The latter can even read a matrix written with
   * writeMatrixToMatlab.
   *
   *
   * @addtogroup ISTL_IO
   * @{
   */

  /** @file
   * @author Markus Blatt
   * @brief Provides classes for reading and writing MatrixMarket Files with
   * an extension for parallel matrices.
   */
  namespace
  {
    /**
     * @brief Helper metaprogram to get
     * the matrix market string representation
     * of the numeric type.
     *
     * Member function mm_numeric_type::c_str()
     * returns the string representation of
     * the type.
     */
    template<class T>
    struct mm_numeric_type {
      enum {
        /**
         * @brief Whethe T is a supported numeric type.
         */
        is_numeric=false
      };
    };

    template<>
    struct mm_numeric_type<int>
    {
      enum {
        /**
         * @brief Whethe T is a supported numeric type.
         */
        is_numeric=true
      };

      static std::string str()
      {
        return "integer";
      }
    };

    template<>
    struct mm_numeric_type<double>
    {
      enum {
        /**
         * @brief Whethe T is a supported numeric type.
         */
        is_numeric=true
      };

      static std::string str()
      {
        return "real";
      }
    };

    template<>
    struct mm_numeric_type<float>
    {
      enum {
        /**
         * @brief Whethe T is a supported numeric type.
         */
        is_numeric=true
      };

      static std::string str()
      {
        return "real";
      }
    };

    template<>
    struct mm_numeric_type<std::complex<double> >
    {
      enum {
        /**
         * @brief Whethe T is a supported numeric type.
         */
        is_numeric=true
      };

      static std::string str()
      {
        return "complex";
      }
    };

    template<>
    struct mm_numeric_type<std::complex<float> >
    {
      enum {
        /**
         * @brief Whethe T is a supported numeric type.
         */
        is_numeric=true
      };

      static std::string str()
      {
        return "complex";
      }
    };

    /**
     * @brief Meta program to write the correct Matrix Market
     * header.
     *
     * Member function mm_header::operator() writes the header.
     *
     * @tparam M The matrix type.
     */
    template<class M>
    struct mm_header_printer;

    template<typename T, typename A, int i, int j>
    struct mm_header_printer<BCRSMatrix<FieldMatrix<T,i,j>,A> >
    {
      static void print(std::ostream& os)
      {
        os<<"%%MatrixMarket matrix coordinate ";
        os<<mm_numeric_type<T>::str()<<" general"<<std::endl;
      }
    };

    template<typename B, typename A>
    struct mm_header_printer<BlockVector<B,A> >
    {
      static void print(std::ostream& os)
      {
        os<<"%%MatrixMarket matrix array ";
        os<<mm_numeric_type<typename B::field_type>::str()<<" general"<<std::endl;
      }
    };

    template<typename T, int j>
    struct mm_header_printer<FieldVector<T,j> >
    {
      static void print(std::ostream& os)
      {
        os<<"%%MatrixMarket matrix array ";
        os<<mm_numeric_type<T>::str()<<" general"<<std::endl;
      }
    };

    template<typename T, int i, int j>
    struct mm_header_printer<FieldMatrix<T,i,j> >
    {
      static void print(std::ostream& os)
      {
        os<<"%%MatrixMarket matrix array ";
        os<<mm_numeric_type<T>::str()<<" general"<<std::endl;
      }
    };

    /**
     * @brief Metaprogram for writing the ISTL block
     * structure header.
     *
     * Member function mm_block_structure_header::print(os, mat) writes
     * the corresponding header to the specified ostream.
     * @tparam The type of the matrix to generate the header for.
     */
    template<class M>
    struct mm_block_structure_header;

    template<typename T, typename A, int i>
    struct mm_block_structure_header<BlockVector<FieldVector<T,i>,A> >
    {
      typedef BlockVector<FieldVector<T,i>,A> M;

      static void print(std::ostream& os, const M& m)
      {
        os<<"% ISTL_STRUCT blocked ";
        os<<i<<" "<<1<<std::endl;
      }
    };

    template<typename T, typename A, int i, int j>
    struct mm_block_structure_header<BCRSMatrix<FieldMatrix<T,i,j>,A> >
    {
      typedef BCRSMatrix<FieldMatrix<T,i,j>,A> M;

      static void print(std::ostream& os, const M& m)
      {
        os<<"% ISTL_STRUCT blocked ";
        os<<i<<" "<<j<<std::endl;
      }
    };


    template<typename T, int i, int j>
    struct mm_block_structure_header<FieldMatrix<T,i,j> >
    {
      typedef FieldMatrix<T,i,j> M;

      static void print(std::ostream& os, const M& m)
      {}
    };

    template<typename T, int i>
    struct mm_block_structure_header<FieldVector<T,i> >
    {
      typedef FieldVector<T,i> M;

      static void print(std::ostream& os, const M& m)
      {}
    };

    enum LineType { MM_HEADER, MM_ISTLSTRUCT, DATA };
    enum { MM_MAX_LINE_LENGTH=1025 };

    enum MM_TYPE { coordinate_type, array_type, unknown_type };

    enum MM_CTYPE { integer_type, double_type, complex_type, pattern, unknown_ctype };

    enum MM_STRUCTURE { general, symmetric, skew_symmetric, hermitian, unknown_structure  };

    struct MMHeader
    {
      MMHeader()
        : type(coordinate_type), ctype(double_type), structure(general)
      {}
      MM_TYPE type;
      MM_CTYPE ctype;
      MM_STRUCTURE structure;
    };

    bool lineFeed(std::istream& file)
    {
      char c;
      if(!file.eof())
        c=file.peek();
      else
        return false;
      // ignore whitespace
      while(c==' ')
      {
        file.get();
        if(file.eof())
          return false;
        c=file.peek();
      }

      if(c=='\n') {
        /* eat the line feed */
        file.get();
        return true;
      }
      return false;
    }

    void skipComments(std::istream& file)
    {
      lineFeed(file);
      char c=file.peek();
      // ignore comment lines
      while(c=='%')
      {
        /* disgard the rest of the line */
        file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        c=file.peek();
      }
    }


    bool readMatrixMarketBanner(std::istream& file, MMHeader& mmHeader)
    {
      std::string buffer;
      char c;
      file >> buffer;
      c=buffer[0];
      mmHeader=MMHeader();
      if(c!='%')
        return false;
      std::cout<<buffer<<std::endl;
      /* read the banner */
      if(buffer!="%%MatrixMarket") {
        /* disgard the rest of the line */
        file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        return false;
      }

      if(lineFeed(file))
        /* premature end of line */
        return false;

      /* read the matrix_type */
      file >> buffer;

      if(buffer != "matrix")
      {
        /* disgard the rest of the line */
        file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        return false;
      }

      if(lineFeed(file))
        /* premature end of line */
        return false;

      /* The type of the matrix */
      file >> buffer;

      if(buffer.empty())
        return false;

      std::transform(buffer.begin(), buffer.end(), buffer.begin(),
                     tolower);

      switch(buffer[0])
      {
      case 'a' :
        /* sanity check */
        if(buffer != "array")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.type=array_type;
        break;
      case 'c' :
        /* sanity check */
        if(buffer != "coordinate")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.type=coordinate_type;
        break;
      default :
        file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        return false;
      }

      if(lineFeed(file))
        /* premature end of line */
        return false;

      /* The numeric type used. */
      file >> buffer;

      if(buffer.empty())
        return false;

      std::transform(buffer.begin(), buffer.end(), buffer.begin(),
                     tolower);
      switch(buffer[0])
      {
      case 'i' :
        /* sanity check */
        if(buffer != "integer")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.ctype=integer_type;
        break;
      case 'r' :
        /* sanity check */
        if(buffer != "real")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.ctype=double_type;
        break;
      case 'c' :
        /* sanity check */
        if(buffer != "complex")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.ctype=complex_type;
        break;
      case 'p' :
        /* sanity check */
        if(buffer != "pattern")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.ctype=pattern;
        break;
      default :
        file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        return false;
      }

      if(lineFeed(file))
        return false;

      file >> buffer;

      std::transform(buffer.begin(), buffer.end(), buffer.begin(),
                     tolower);
      switch(buffer[0])
      {
      case 'g' :
        /* sanity check */
        if(buffer != "general")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.structure=general;
        break;
      case 'h' :
        /* sanity check */
        if(buffer != "hermitian")
        {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
        mmHeader.structure=hermitian;
        break;
      case 's' :
        if(buffer.size()==1) {
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }

        switch(buffer[1])
        {
        case 'y' :
          /* sanity check */
          if(buffer != "symmetric")
          {
            file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
            return false;
          }
          mmHeader.structure=symmetric;
          break;
        case 'k' :
          /* sanity check */
          if(buffer != "skew-symmetric")
          {
            file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
            return false;
          }
          mmHeader.structure=skew_symmetric;
          break;
        default :
          file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          return false;
        }
      default :
        file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        return false;
      }
      file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
      c=file.peek();
      return true;

    }

    void readNextLine(std::istream& file, std::ostringstream& line, LineType& type)
    {
      char c;
      std::size_t index=0;

      //empty lines will be disgarded and we will simply read the next line
      while(index==0&&!file.eof())
      {
        // strip spaces
        while(!file.eof() && (c=file.get())==' ') ;

        //read the rest of the line until comment
        while(!file.eof() && (c=file.get())=='\n') {
          switch(c)
          {
          case '%' :
            /* disgard the rest of the line */
            file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
          }
        }
      }

      //      buffer[index]='\0';
    }

    template<std::size_t brows, std::size_t bcols>
    Dune::tuple<std::size_t, std::size_t, std::size_t>
    calculateNNZ(std::size_t rows, std::size_t cols, std::size_t entries, const MMHeader& header)
    {
      std::size_t blockrows=rows/brows;
      std::size_t blockcols=cols/bcols;
      std::size_t blocksize=brows*bcols;
      std::size_t blockentries=0;

      switch(header.structure)
      {
      case general :
        blockentries = entries/blocksize; break;
      case skew_symmetric :
        blockentries = 2*entries/blocksize; break;
      case symmetric :
        blockentries = (2*entries-rows)/blocksize; break;
      case hermitian :
        blockentries = (2*entries-rows)/blocksize; break;
      default :
        throw Dune::NotImplemented();
      }
      return Dune::make_tuple(blockrows, blockcols, blockentries);
    }

    /*
     *   @brief Storage class for the column index and the numeric value.
     *
     * \tparam T Either a NumericWrapper of the numeric type or PatternDummy
     *     for MatrixMarket pattern case.
     */
    template<typename T>
    struct IndexData : public T
    {
      std::size_t index;
    };


    /**
     * @brief a wrapper class of numeric values.
     *
     * Use for template specialization to catch the pattern
     * type MatrixMarket matrices. Uses Empty base class optimization
     * in the pattern case.
     *
     * @tparam T Either a NumericWrapper of the numeric type or PatternDummy
     *     for MatrixMarket pattern case.
     */
    template<typename T>
    struct NumericWrapper
    {
      T number;
      operator T&()
      {
        return number;
      }
    };

    /**
     * @brief Utility class for marking the pattern type of the MatrixMarket matrices.
     */
    struct PatternDummy
    {};

    template<>
    struct NumericWrapper<PatternDummy>
    {};

    template<typename T>
    std::istream& operator>>(std::istream& is, NumericWrapper<T>& num)
    {
      return is>>num.number;
    }

    std::istream& operator>>(std::istream& is, NumericWrapper<PatternDummy>& num)
    {
      return is;
    }

    /**
     * @brief LessThan operator.
     *
     * It simply compares the index.
     */
    template<typename T>
    bool operator<(const IndexData<T>& i1, const IndexData<T>& i2)
    {
      return i1.index<i2.index;
    };

    /**
     * @brief Read IndexData from a stream.
     * @param is The input stream we read.
     * @param data Where to store the read data.
     */
    template<typename T>
    std::istream& operator>>(std::istream& is, IndexData<T>& data)
    {
      is>>data.index;
      /* MatrixMarket indices are one based. Decrement for C++ */
      --data.index;
      return is>>data.number;
    }

    /**
     * @brief Functor to the data values of the matrix.
     *
     * This is specialized for PatternDummy. The specialization does not
     * set anything.
     */
    template<typename D, int brows, int bcols>
    struct MatrixValuesSetter
    {
      /**
       * @brief Sets the matrixvalues.
       * @param row The row data as read from file.
       * @param matrix The matrix whose data we set.
       */
      template<typename M>
      void operator()(const std::vector<std::set<IndexData<D> > >& rows,
                      M& matrix)
      {
        for(typename M::RowIterator iter=matrix.begin();
            iter!= matrix.end(); ++iter)
        {
          for(typename M::size_type brow=iter.index()*brows,
              browend=iter.index()*brows+brows;
              brow<browend; ++brow)
          {
            typedef typename std::set<IndexData<D> >::const_iterator Siter;
            for(Siter siter=rows[brow].begin(), send=rows[brow].end();
                siter != send; ++siter)
              (*iter)[siter->index/bcols][brow%brows][siter->index%bcols]=siter->number;
          }
        }
      }
    };

    template<int brows, int bcols>
    struct MatrixValuesSetter<PatternDummy,brows,bcols>
    {
      template<typename M>
      void operator()(const std::vector<std::set<IndexData<PatternDummy> > >& rows,
                      M& matrix)
      {}
    };

    template<typename T, typename A, int brows, int bcols, typename D>
    void readSparseEntries(Dune::BCRSMatrix<Dune::FieldMatrix<T,brows,bcols>,A>& matrix,
                           std::istream& file, std::size_t entries,
                           const MMHeader& mmHeader, const D&)
    {
      typedef Dune::BCRSMatrix<Dune::FieldMatrix<T,brows,bcols>,A> Matrix;
      // First path
      // store entries together with column index in a speparate
      // data structure
      std::vector<std::set<IndexData<D> > > rows(matrix.N()*brows);

      for(; entries>0; --entries) {
        std::size_t row;
        IndexData<D> data;
        skipComments(file);
        file>>row;
        --row; // Index was 1 based.
        assert(row/bcols<matrix.N());
        file>>data;
        assert(data.index/bcols<matrix.M());
        rows[row].insert(data);
      }

      // TODO extend to capture the nongeneral cases.
      if(mmHeader.structure!= general)
        DUNE_THROW(Dune::NotImplemented, "Only general is supported right now!");

      // Setup the matrix sparsity pattern
      int nnz=0;
      for(typename Matrix::CreateIterator iter=matrix.createbegin();
          iter!= matrix.createend(); ++iter)
      {
        for(std::size_t brow=iter.index()*brows, browend=iter.index()*brows+brows;
            brow<browend; ++brow)
        {
          typedef typename std::set<IndexData<D> >::const_iterator Siter;
          for(Siter siter=rows[brow].begin(), send=rows[brow].end();
              siter != send; ++siter, ++nnz)
            iter.insert(siter->index/bcols);
        }
      }

      //Set the matrix values
      matrix=0;

      MatrixValuesSetter<D,brows,bcols> Setter;

      Setter(rows, matrix);
    }
  } // end anonymous namespace

  class MatrixMarketFormatError : public Dune::Exception
  {};


  void mm_read_header(std::size_t& rows, std::size_t& cols, MMHeader& header, std::istream& istr,
                      bool isVector)
  {
    if(!readMatrixMarketBanner(istr, header)) {
      std::cerr << "First line was not a correct Matrix Market banner. Using default:\n"
                << "%%MatrixMarket matrix coordinate real general"<<std::endl;
      // Go to the beginning of the file
      istr.clear() ;
      istr.seekg(0, std::ios::beg);
      if(isVector)
        header.type=array_type;
    }

    skipComments(istr);

    if(lineFeed(istr))
      throw MatrixMarketFormatError();

    istr >> rows;

    if(lineFeed(istr))
      throw MatrixMarketFormatError();
    istr >> cols;
  }

  template<typename T, typename A, int entries>
  void mm_read_vector_entries(Dune::BlockVector<Dune::FieldVector<T,entries>,A>& vector,
                              std::size_t size,
                              std::istream& istr)
  {
    for(int i=0; size>0; ++i, --size) {
      T val;
      istr>>val;
      vector[i/entries][i%entries]=val;
    }
  }


  /**
   * @brief Reads a sparse matrix from a matrix market file.
   * @param matrix The matrix to store the data in.
   * @param istr The input stream to read the data from.
   * @warning Not all formats are supported!
   */
  template<typename T, typename A, int entries>
  void readMatrixMarket(Dune::BlockVector<Dune::FieldVector<T,entries>,A>& vector,
                        std::istream& istr)
  {
    MMHeader header;
    std::size_t rows, cols;
    mm_read_header(rows,cols,header,istr, true);
    if(cols!=1)
      DUNE_THROW(MatrixMarketFormatError, "cols!=1, therefore this is no vector!");

    if(header.type!=array_type)
      DUNE_THROW(MatrixMarketFormatError, "Vectors have to be stored in array format!");

    std::size_t size=rows/entries;
    if(size*entries!=rows)
      DUNE_THROW(MatrixMarketFormatError, "Block size of vector is not correct1");

    vector.resize(size);

    istr.ignore(std::numeric_limits<std::streamsize>::max(),'\n');

    mm_read_vector_entries(vector, rows, istr);
  }


  /**
   * @brief Reads a sparse matrix from a matrix market file.
   * @param matrix The matrix to store the data in.
   * @param istr The input stream to read the data from.
   * @warning Not all formats are supported!
   */
  template<typename T, typename A, int brows, int bcols>
  void readMatrixMarket(Dune::BCRSMatrix<Dune::FieldMatrix<T,brows,bcols>,A>& matrix,
                        std::istream& istr)
  {

    typedef Dune::BCRSMatrix<Dune::FieldMatrix<double,brows,bcols> > Matrix;

    MMHeader header;
    if(!readMatrixMarketBanner(istr, header)) {
      std::cerr << "First line was not a correct Matrix Market banner. Using default:\n"
                << "%%MatrixMarket matrix coordinate real general"<<std::endl;
      // Go to the beginning of the file
      istr.clear() ;
      istr.seekg(0, std::ios::beg);
    }
    skipComments(istr);

    std::size_t rows, cols, entries;

    if(lineFeed(istr))
      throw MatrixMarketFormatError();

    istr >> rows;

    if(lineFeed(istr))
      throw MatrixMarketFormatError();
    istr >> cols;

    if(lineFeed(istr))
      throw MatrixMarketFormatError();

    istr >>entries;

    std::size_t nnz, blockrows, blockcols;

    Dune::tie(blockrows, blockcols, nnz) = calculateNNZ<brows, bcols>(rows, cols, entries, header);

    istr.ignore(std::numeric_limits<std::streamsize>::max(),'\n');


    matrix.setSize(blockrows, blockcols);
    matrix.setBuildMode(Dune::BCRSMatrix<Dune::FieldMatrix<T,brows,bcols>,A>::row_wise);

    if(header.type==array_type)
      DUNE_THROW(Dune::NotImplemented, "Array format currently not supported for matrices!");

    readSparseEntries(matrix, istr, entries, header, NumericWrapper<double>());
  }

  template<typename M>
  struct mm_multipliers
  {};

  template<typename B, int i, int j, typename A>
  struct mm_multipliers<BCRSMatrix<FieldMatrix<B,i,j>,A> >
  {
    enum {
      rows = i,
      cols = j
    };
  };

  template<typename B, int i, int j>
  void mm_print_entry(const FieldMatrix<B,i,j>& entry,
                      typename FieldMatrix<B,i,j>::size_type rowidx,
                      typename FieldMatrix<B,i,j>::size_type colidx,
                      std::ostream& ostr)
  {
    typedef typename FieldMatrix<B,i,j>::const_iterator riterator;
    typedef typename FieldMatrix<B,i,j>::ConstColIterator citerator;
    for(riterator row=entry.begin(); row != entry.end(); ++row, ++rowidx) {
      int coli=colidx;
      for(citerator col = row->begin(); col != row->end(); ++col, ++coli)
        ostr<< rowidx<<" "<<coli<<" "<<*col<<std::endl;
    }
  }

  // Write a vector entry
  template<typename V>
  void mm_print_vector_entry(const V& entry, std::ostream& ostr,
                             const integral_constant<int,1>&)
  {
    ostr<<entry<<std::endl;
  }

  // Write a vector
  template<typename V>
  void mm_print_vector_entry(const V& vector, std::ostream& ostr,
                             const integral_constant<int,0>&)
  {
    // Is the entry a supported numeric type?
    const int isnumeric = mm_numeric_type<typename V::block_type>::is_numeric;
    typedef typename V::const_iterator VIter;

    for(VIter i=vector.begin(); i != vector.end(); ++i)

      mm_print_vector_entry(*i, ostr,
                            integral_constant<int,isnumeric>());
  }

  template<typename T, typename A, int i>
  std::size_t countEntries(const BlockVector<FieldVector<T,i>,A>& vector)
  {
    return vector.size()*i;
  }

  // Version for writing vectors.
  template<typename V>
  void writeMatrixMarket(const V& vector, std::ostream& ostr,
                         const integral_constant<int,0>&)
  {
    ostr<<countEntries(vector)<<" "<<1<<std::endl;
    const int isnumeric = mm_numeric_type<typename V::block_type>::is_numeric;
    mm_print_vector_entry(vector,ostr, integral_constant<int,isnumeric>());
  }

  // Versions for writing matrices
  template<typename M>
  void writeMatrixMarket(const M& matrix,
                         std::ostream& ostr,
                         const integral_constant<int,1>&)
  {
    ostr<<matrix.M()*mm_multipliers<M>::rows<<" "
        <<matrix.N()*mm_multipliers<M>::cols<<" "
        <<countNonZeros(matrix)<<std::endl;

    typedef typename M::const_iterator riterator;
    typedef typename M::ConstColIterator citerator;
    for(riterator row=matrix.begin(); row != matrix.end(); ++row)
      for(citerator col = row->begin(); col != row->end(); ++col)
        // Matrix Market indexing start with 1!
        mm_print_entry(*col, row.index()*mm_multipliers<M>::rows+1,
                       col.index()*mm_multipliers<M>::cols+1, ostr);
  }


  /**
   * @brief writes a ISTL matrix or vector to a stream in matrix market format.
   */
  template<typename M>
  void writeMatrixMarket(const M& matrix,
                         std::ostream& ostr)
  {
    // Write header information
    mm_header_printer<M>::print(ostr);
    mm_block_structure_header<M>::print(ostr,matrix);
    // Choose the correct function for matrix and vector
    writeMatrixMarket(matrix,ostr,integral_constant<int,IsMatrix<M>::value>());
  }


  /**
   * @brief Stores a parallel matrix/vector in matrix market format in a file.
   *
   * More about the matrix market exchange format can be found
   * <a href="http://math.nist.gov/MatrixMarket/formats.html">here</a>.
   *
   * @param matrix The matrix/vector to store.
   * @param filename the name of the filename (without suffix and rank!)
   *        rank i will write the file filename_i.mm
   */
  template<typename M>
  void storeMatrixMarket(const M& matrix,
                         std::string filename)
  {
    std::ofstream file(filename.c_str());
    file.setf(std::ios::scientific,std::ios::floatfield);
    writeMatrixMarket(matrix, file);
    file.close();
  }

#if HAVE_MPI
  /**
   * @brief Stores a parallel matrix/vector in matrix market format in a file.
   *
   * More about the matrix market exchange format can be found
   * <a href="http://math.nist.gov/MatrixMarket/formats.html">here</a>.
   *
   * @param matrix The matrix/vector to store.
   * @param filename the name of the filename (without suffix and rank!)
   *        rank i will write the file filename_i.mm
   * @param comm The information about the data distribution.
   * @param storeIndices Whether to store the parallel index information.
   *        If true rank i writes the index information to file filename_i.idx.
   */
  template<typename M, typename G, typename L>
  void storeMatrixMarket(const M& matrix,
                         std::string filename,
                         const OwnerOverlapCopyCommunication<G,L>& comm,
                         bool storeIndices=true)
  {
    // Get our rank
    int rank = comm.communicator().rank();
    // Write the local matrix
    std::ostringstream rfilename;
    rfilename<<filename <<"_"<<rank<<".mm";
    std::cout<< rfilename.str()<<std::endl;
    std::ofstream file(rfilename.str().c_str());
    file.setf(std::ios::scientific,std::ios::floatfield);
    writeMatrixMarket(matrix, file);
    file.close();

    if(!storeIndices)
      return;

    // Write the global to local index mapping
    rfilename.str("");
    rfilename<<filename<<"_"<<rank<<".idx";
    file.open(rfilename.str().c_str());
    file.setf(std::ios::scientific,std::ios::floatfield);
    typedef typename OwnerOverlapCopyCommunication<G,L>::ParallelIndexSet IndexSet;
    typedef typename IndexSet::const_iterator Iterator;
    for(Iterator iter = comm.indexSet().begin();
        iter != comm.indexSet().end(); ++iter) {
      file << iter->global()<<" "<<(std::size_t)iter->local()<<" "
           <<(int)iter->local().attribute()<<" "<<(int)iter->local().isPublic()<<std::endl;
    }
    // Store neighbour information for efficient remote indices setup.
    file<<"neighbours:";
    const std::set<int>& neighbours=comm.remoteIndices().getNeighbours();
    typedef std::set<int>::const_iterator SIter;
    for(SIter neighbour=neighbours.begin(); neighbour != neighbours.end(); ++neighbour) {
      file<<" "<< *neighbour;
    }
    file.close();
  }

  /**
   * @brief Load a parallel matrix/vector stored in matrix market format.
   *
   * More about the matrix market exchange format can be found
   * <a href="http://math.nist.gov/MatrixMarket/formats.html">here</a>.
   *
   * @param matrix Where to store the matrix/vector.
   * @param filename the name of the filename (without suffix and rank!)
   *        rank i will read the file filename_i.mm
   * @param comm The information about the data distribution.
   * @param readIndices Whether to read the parallel index information.
   *        If true rank i reads the index information form file filename_i.idx
   *        And builds the remote indices information.
   */
  template<typename M, typename G, typename L>
  void loadMatrixMarket(M& matrix,
                        const std::string& filename,
                        OwnerOverlapCopyCommunication<G,L>& comm,
                        bool readIndices=true)
  {
    typedef typename OwnerOverlapCopyCommunication<G,L>::ParallelIndexSet::LocalIndex LocalIndex;
    typedef typename LocalIndex::Attribute Attribute;
    // Get our rank
    int rank = comm.communicator().rank();
    // load local matrix
    std::ostringstream rfilename;
    rfilename<<filename <<"_"<<rank<<".mm";
    std::ifstream file;
    file.open(rfilename.str().c_str(), std::ios::in);
    if(!file)
      DUNE_THROW(IOError, "Could not open file" << rfilename.str().c_str());
    //if(!file.is_open())
    //
    readMatrixMarket(matrix,file);
    file.close();

    if(!readIndices)
      return;

    // read indices
    typedef typename OwnerOverlapCopyCommunication<G,L>::ParallelIndexSet IndexSet;
    IndexSet& pis=comm.pis;
    rfilename.str("");
    rfilename<<filename<<"_"<<rank<<".idx";
    file.open(rfilename.str().c_str());
    if(pis.size()!=0)
      DUNE_THROW(InvalidIndexSetState, "Index set is not empty!");

    pis.beginResize();
    while(!file.eof() && file.peek()!='n') {
      G g;
      file >>g;
      std::size_t l;
      file >>l;
      int c;
      file >>c;
      bool b;
      file >> b;
      pis.add(g,LocalIndex(l,Attribute(c),b));
      lineFeed(file);
    }
    pis.endResize();
    if(!file.eof()) {
      // read neighbours
      std::string s;
      file>>s;
      if(s!="neighbours:")
        DUNE_THROW(MatrixMarketFormatError, "was exspecting the string: \"neighbours:\"");
      std::set<int> nb;
      while(!file.eof()) {
        int i;
        file >> i;
        nb.insert(i);
      }
      file.close();
      comm.ri.setNeighbours(nb);
    }
    comm.ri.template rebuild<false>();
  }

  #endif

  /**
   * @brief Load a matrix/vector stored in matrix market format.
   *
   * More about the matrix market exchange format can be found
   * <a href="http://math.nist.gov/MatrixMarket/formats.html">here</a>.
   *
   * @param matrix Where to store the matrix/vector.
   * @param filename the name of the filename (without suffix and rank!)
   *        rank i will read the file filename_i.mm
   */
  template<typename M>
  void loadMatrixMarket(M& matrix,
                        const std::string& filename)
  {
    std::ifstream file;
    file.open(filename.c_str(), std::ios::in);
    if(!file)
      DUNE_THROW(IOError, "Could not open file" << filename);
    readMatrixMarket(matrix,file);
    file.close();
  }

  /** @} */
}
#endif
