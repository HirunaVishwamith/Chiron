// hart_alu.inc — ALU helpers: sign-extend, high-multiply, integer divide
//
// Fragment of class hart (see hart.h) — included inside the class body;
// not a standalone header. Kept separate to keep hart.h readable.

  template <class T>
  T sign_extend(const T &x, const int &bits)
  {
    T m = 1;
    m <<= bits - 1;
    return (x ^ m) - m;
  }

  template <class T>
  T divi(T num1, T num2, int s)
  {
    if (num2 == 0)
    {
      switch (s)
      {
      case 0:
        return (T)(-1);
      case 1:
        return (T)MASK64;
      case 2:
        return num1;
      case 3:
        return num1;
      }
    }
    else if (num1 == (-pow(2ull, 63)) && num2 == -1)
    {
      if (s == 0 || s == 2)
      {
        switch (s)
        {
        case 0:
          return (T)(-pow(2ull, 63));
        case 2:
          return 0;
        }
      }
    }
    else
    {
      ldiv_t div_result;
      switch (s)
      {
      case 0:
        div_result = div((int64_t)num1, (int64_t)num2);
        return div_result.quot;
      case 1:
        return num1 / num2;
      case 2:
        div_result = div((int64_t)num1, (int64_t)num2);
        return div_result.rem;
      case 3:
        return num1 % num2;
      default:
        return (T)(-1);
      }
    }
    return (T)(-1);
  }

  template <class T>
  T divi32(T num1, T num2, int s)
  {
    if (num2 == 0)
    {
      switch (s)
      {
      case 0:
        return (T)(-1);
      case 1:
        return (T)MASK32;
      case 2:
        return num1;
      case 3:
        return num1;
      }
    }
    else if (num1 == (-pow(2, 31)) && num2 == -1)
    {
      if (s == 0 || s == 2)
      {
        switch (s)
        {
        case 0:
          return (T)(-pow(2ull, 31));
        case 2:
          return 0;
        }
      }
    }
    else
    {
      div_t div_result;
      switch (s)
      {
      case 0:
        div_result = div((int32_t)num1, (int32_t)num2);
        return div_result.quot;
      case 1:
        return num1 / num2;
      case 2:
        div_result = div((int32_t)num1, (int32_t)num2);
        return div_result.rem;
      case 3:
        return num1 % num2;
      default:
        return (T)(-1);
      }
    }
    return (T)(-1);
  }
