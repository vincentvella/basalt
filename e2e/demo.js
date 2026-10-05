'use strict';

// A React Native surface with no React in it.
//
// This talks straight to `nativeFabricUIManager`, the JSI binding React's
// Fabric renderer normally drives. Doing it by hand keeps the first light-up
// honest: there is no Metro bundle, no AppRegistry and no react package, so
// everything on screen comes from Hermes running this file, committing a
// shadow tree, and Fabric diffing it into the mutations the GTK mounting
// manager already knows how to apply.
//
// The protocol is Fabric's persistent tree:
//
//   createNode(tag, componentName, surfaceId, props, instanceHandle)
//   appendChild(parentNode, childNode)          -- while the parent is unsealed
//   createChildSet() / appendChildToSet(set, n) -- the root's children
//   completeRoot(surfaceId, childSet)           -- commit
//
// A second commit clones nodes rather than recreating them. That is what React
// does, and it is what makes Fabric emit Update and Remove instead of tearing
// the whole tree down and rebuilding it.

(function () {
  // The only component with a GTK peer today. GtkMountingManager::hasComponent
  // says so, and LinuxComponentRegistry registers only ViewComponentDescriptor.
  var VIEW = 'View';

  // Colours reach C++ as a signed 32-bit ARGB integer -- the same thing RN's
  // processColor() produces. `| 0` is what makes it signed.
  function argb(value) {
    return value | 0;
  }

  var BLUE = argb(0xff4285f4);
  var PURPLE = argb(0xff9b59f6);
  var ORANGE = argb(0xfff26f56);
  var GREEN = argb(0xff56c98a);
  var PALE = argb(0xffe6eeff);

  // Tags identify views across commits; Fabric diffs by tag. Any unique
  // positive integer works as long as it does not collide with the surface id,
  // which is itself the root node's tag.
  var nextTag = 100;

  // Nodes from the last commit, so the next one can clone them.
  var tree = null;

  var fabric = null;

  function create(props, children) {
    var node = fabric.createNode(nextTag++, VIEW, tree.surfaceId, props, {});
    if (children) {
      for (var i = 0; i < children.length; i++) {
        fabric.appendChild(node, children[i]);
      }
    }
    return node;
  }

  function childSet(nodes) {
    var set = fabric.createChildSet();
    for (var i = 0; i < nodes.length; i++) {
      fabric.appendChildToSet(set, nodes[i]);
    }
    return set;
  }

  // ---------------------------------------------------------------------
  // Commit 1 -- flexbox. Nothing here says where a box goes; Yoga decides.
  // ---------------------------------------------------------------------

  function buildFirstTree() {
    // A nested child, to prove children are laid out relative to their parent
    // and clipped or overflowed by it rather than by the root.
    tree.nested = create({
      backgroundColor: PALE,
      width: 160,
      height: 90,
      margin: 24,
    });

    tree.blue = create(
      {
        backgroundColor: BLUE,
        flex: 1,
        marginRight: 16,
      },
      [tree.nested]
    );

    tree.orange = create({
      backgroundColor: ORANGE,
      flex: 1,
    });

    // flexDirection defaults to 'column' in React Native, not 'row' as in CSS.
    tree.topRow = create(
      {
        flexDirection: 'row',
        flex: 1,
        marginBottom: 16,
      },
      [tree.blue, tree.orange]
    );

    tree.green = create({
      backgroundColor: GREEN,
      height: 160,
    });

    tree.container = create(
      {
        flex: 1,
        padding: 24,
      },
      [tree.topRow, tree.green]
    );

    return [tree.container];
  }

  // ---------------------------------------------------------------------
  // Commit 2 -- the same tree, changed. Recolour and resize one view, drop
  // another. Cloning preserves node identity, so Fabric emits Update for the
  // first and Remove+Delete for the second.
  // ---------------------------------------------------------------------

  function buildSecondTree() {
    // Same node, new props: an Update, and a re-layout of its subtree.
    var blue = fabric.cloneNodeWithNewProps(tree.blue, {
      backgroundColor: PURPLE,
      flex: 2,
      marginRight: 16,
    });

    // The orange view is simply absent from the new child list.
    var topRow = fabric.cloneNodeWithNewChildren(tree.topRow, childSet([blue]));

    var green = fabric.cloneNodeWithNewProps(tree.green, {
      backgroundColor: GREEN,
      height: 260,
    });

    var container = fabric.cloneNodeWithNewChildren(
      tree.container,
      childSet([topRow, green])
    );

    tree.blue = blue;
    tree.topRow = topRow;
    tree.green = green;
    tree.container = container;

    return [container];
  }

  // ---------------------------------------------------------------------
  // Entry point. The host calls this after ReactHost::startSurface, because
  // completeRoot needs the surface's shadow tree to already be registered.
  // ---------------------------------------------------------------------

  // ReactHost::stopSurface calls into JS for this on every shutdown, even for a
  // surface started with an empty module name: SurfaceHandler::start guards on
  // the module name, but UIManager::stopSurface does not. React Native's own
  // AppRegistry installs it, so a script with no React has to install it
  // itself or teardown reports a fatal JS error. Fabric commits the empty tree
  // from C++, so there is nothing to unmount here -- this only drops the node
  // references this script is holding.
  globalThis.RN$stopSurface = function (surfaceId) {
    if (tree != null && tree.surfaceId === surfaceId) {
      tree = null;
    }
  };

  globalThis.basaltRender = function (surfaceId, step) {
    fabric = globalThis.nativeFabricUIManager;
    if (fabric == null) {
      throw new Error('nativeFabricUIManager is not installed on this runtime');
    }

    if (step === 1) {
      tree = { surfaceId: surfaceId };
    } else if (tree == null) {
      throw new Error('commit ' + step + ' has no previous tree to clone');
    }

    var roots = step === 1 ? buildFirstTree() : buildSecondTree();
    fabric.completeRoot(surfaceId, childSet(roots));
  };
})();
