--TEST--
Ensure Iterators have their memory dumped
--SKIPIF--
<?php
    if (!extension_loaded('json')) die('skip json ext not loaded');
?>
--FILE--
<?php
    $dump = fopen('php://memory', 'rw');

    class MyIterator implements Iterator {
        private $position = 0;
        private $array = array(
            "firstelement",
            "secondelement",
            "lastelement",
        );  

        public function __construct() {
            $this->position = 0;
        }

        public function rewind() {
            $this->position = 0;
        }

        public function current() {
            return $this->array[$this->position];
        }

        public function key() {
            return $this->position;
        }

        public function next() {
            ++$this->position;
        }

        public function valid() {
            return isset($this->array[$this->position]);
        }
    }
    
    foreach((new MyIterator()) as $key => $value) {
        meminfo_dump($dump);
        break;
    }

    rewind($dump);
    $meminfoData = json_decode(stream_get_contents($dump), true);
    fclose($dump);

    var_dump($meminfoData);
    $objectsCount = 0;
    foreach ($meminfoData['items'] as $item) {
        if (isset($item['symbol_name']) && $item['symbol_name'] == 'myObj') {
            echo "Symbol: " . $item['symbol_name'] . "\n";
            echo "  Frame: " . $item['frame'] . "\n";
            echo "  Type: " . $item['type'] . "\n";
            echo "  Is root: " . $item['is_root'] . "\n";
            echo "  Children:\n";
            if (isset($item['children'])) {
                foreach($item['children'] as $symbol => $address) {
                    echo "    $symbol\n";
                }
            }
        }
    }

?>
--EXPECT--
Symbol: myObj
  Frame: <GLOBAL>
  Type: object
  Is root: 1
  Children:
    myMember
