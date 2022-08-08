--TEST--
Check that a class that overrides __debugInfo is accounted for
--SKIPIF--
<?php
    if (!extension_loaded('json')) die('skip json ext not loaded');
?>
--FILE--
<?php
    $dump = fopen('php://memory', 'rw');

    class MyClass
    {
        private $myMember = 'hidden from var_dump';
        
        public function __debugInfo() {
            return [];
        }
    }
    
    $myObj = new MyClass();

    meminfo_dump($dump);

    rewind($dump);
    $meminfoData = json_decode(stream_get_contents($dump), true);
    fclose($dump);

    $objectsCount = 0;
    foreach ($meminfoData['items'] as $item) {
        if (isset($item['symbol_name']) && $item['symbol_name'] == 'myObj') {
            echo "Symbol: " . $item['symbol_name'] . "\n";
            echo "  Frame: " . $item['frame'] . "\n";
            echo "  Type: " . $item['type'] . "\n";
            echo "  Is root: " . $item['is_root'] . "\n";
            if (!empty($item['children'])) {
                echo "  Children:\n";
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
