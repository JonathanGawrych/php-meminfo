--TEST--
Ensure Closures have their memory dumped (fixing __debugInfo broke this)
--SKIPIF--
<?php
    if (!extension_loaded('json')) die('skip json ext not loaded');
?>
--FILE--
<?php
    $dump = fopen('php://memory', 'rw');

    class MyClass
    {
        public $myMember;
        public function getClosure() {
            $localVarToUse = 'some string';
            return function ($someParam) use ($localVarToUse) {
                static $localStaticVariable;
                $localStaticVariable = $localVarToUse;
                $this->myMember = $someParam;
            };
        }
    }
    
    $closure = (new MyClass())->getClosure();

    meminfo_dump($dump);

    rewind($dump);
    $meminfoData = json_decode(stream_get_contents($dump), true);
    fclose($dump);
    
    function printItemRecursive ($meminfoData, $propName, $address) {
        $item = $meminfoData['items'][$address];
        if ($propName !== null) {
            echo "Property Name: " . $propName . "\n";
        }
        if (isset($item['symbol_name'])) {
            echo "Symbol: " . $item['symbol_name'] . "\n";
        }
        if (isset($item['frame'])) {
            echo "  Frame: " . $item['frame'] . "\n";
        }
        echo "  Type: " . $item['type'] . "\n";
        echo "  Is root: " . ($item['is_root'] ?: '0') . "\n";
        if (!empty($item['children'])) {
            echo "  Children:\n";
            foreach($item['children'] as $propName => $childAddress) {
                echo "    $propName\n";
            }
        }
        foreach($item['children'] ?? [] as $propName => $childAddress) {
            printItemRecursive($meminfoData, $propName, $childAddress);
        }
    };
    
    foreach ($meminfoData['items'] as $address => $item) {
        if (isset($item['symbol_name']) && $item['symbol_name'] == 'closure') {
            printItemRecursive($meminfoData, null, $address);
            break;
        }
    }

?>
--EXPECT--
Symbol: closure
  Frame: <GLOBAL>
  Type: object
  Is root: 1
  Children:
    localVarToUse
    localStaticVariable
    this
Property Name: localVarToUse
  Type: string
  Is root: 0
Property Name: localStaticVariable
  Type: null
  Is root: 0
Property Name: this
  Type: object
  Is root: 0
  Children:
    myMember
Property Name: myMember
  Type: null
  Is root: 0
