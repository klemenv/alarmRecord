template <typename T>
class CircularList {
    private:
        struct Node {
            struct Node* prev = this;
            struct Node* next = this;
            T data;
        };
        Node* nodes;
        size_t size = 1;
    
    public:
        CircularList()
        {
            nodes = new Node();
        }

        void resize(size_t len)
        {
            if (len < 1) {
                len = 1;
            }

            while (len > size) {
                Node* node = new Node();
                node->prev = nodes;
                node->next = nodes->next;
                nodes->next = node;
                if (nodes->prev == nodes) {
                    nodes->prev = node;
                } else {
                    nodes->prev->prev = node;
                }
                size++;
            }

            while (size > len) {
                Node* node = nodes;
                nodes->next->prev = nodes->prev;
                nodes->prev->next = nodes->next;
                nodes = nodes->prev;
                delete node; 
                size--;
            }
        }

        void advance()
        {
            nodes = nodes->next;
        }

        T& current()
        {
            return nodes->data;
        }

        T& previous()
        {
            return nodes->prev->data;
        }

        T& last()
        {
            return nodes->next->data;
        }

        T& print()
        {
            auto n = nodes;
            printf("size=%d\n", size);
            for (int i = 0; i < size; i++) {
                printf("%p next=%p prev=%p\n", n, n->next, n->prev);
                n = n->prev;
            }
        }

};