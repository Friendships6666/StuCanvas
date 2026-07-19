import std;

int main ()
{
    std::vector< std::string > words = { "Hello", "from", "modern", "C++23", "modules!" };
    std::sort ( words.begin (), words.end () );
    for ( const auto& w : words )
    {
        std::cout << w << " ";
    }
    std::cout << std::endl;
    return 0;
}
